#include "Halide.h"
#include "halide/common_halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;
using namespace Halide::ConciseCasts;

namespace hannk {

Var x("x"), y("y"), c("c"), b("b");
Var ci("ci"), co("co");

// There are two codepaths in this generator. On targets with widening
// 8-bit multiplies, we implement the reduction by expanding the subtraction
// of the offsets into 4 reductions involving 8-bit multiplies. On targets
// without widening 8-bit multiplication, it's faster to just subtract the
// offsets and use 16-bit multiplications.
bool use_8bit_multiply(const Target &target) {
    return target.arch != Target::X86 || target.has_feature(Target::AVX512_SapphireRapids);
}

// How many registers to use as accumulators, as a function of the target.
int get_accumulator_count(const Target &target) {
    if (target.has_feature(Target::HVX)) {
        // Hexagon has dot products between vector and scalar registers, so
        // we don't need to use any vector registers for the input, so we
        // can use a lot of registers as accumulators without spilling to
        // the stack.
        return 24;
    } else if (get_register_count(target) >= 32) {
        return 20;
    } else {
        return 8;
    }
}

class Conv : public Generator<Conv> {
public:
    // How much to unroll the reduction loop over channels. On some targets,
    // loading a few scalars for one of the reduction inputs is fine, and avoids
    // a large alignment requirement. However, on other targets, it is beneficial
    // to load vectors, so making this value larger helps for big reductions.
    GeneratorParam<int> unroll_reduction_{"unroll_reduction", 4};

    // Unsigned 8-bit input tensor, indexed by c, x, y, b.
    Input<Buffer<uint8_t, 4>> input_{"input"};
    Input<uint8_t> input_zero_{"input_zero"};

    // A 6D array of filter coefficients indexed by ci % n, co % k, ci / n, co / k, x, y,
    // where n = vector_reduction and k = accum_vector_size (below).
    Input<Buffer<void, 6>> filter_{"filter"};
    Input<uint8_t> filter_zero_{"filter_zero"};

    // A 1D array of 32-bit biases. The bias should be added to the c
    // dimension of the output.
    Input<Buffer<int32_t, 1>> bias_{"bias"};

    // The stride specifies how the input [x, y] is sub-subsampled. For every
    // spatial location [x, y] in the output buffer, the input buffer is sampled
    // spatially at [x * stride, y * stride]. The caller is responsible for
    // allocating the correct output memory.
    Input<int> stride_x_{"stride_x"};
    Input<int> stride_y_{"stride_y"};
    Input<int> dilation_x_{"dilation_x"};
    Input<int> dilation_y_{"dilation_y"};

    Input<int32_t> output_multiplier_{"output_multiplier"};
    Input<int32_t> output_shift_{"output_shift"};
    Input<uint8_t> output_zero_{"output_zero"};
    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Output<Buffer<void, 4>> output_{"output"};

    void configure() {
        if (use_8bit_multiply(target)) {
            filter_.set_type(UInt(8));
        } else {
            filter_.set_type(Int(16));
        }
    }

    void generate() {
        // The algorithm.
        Func input("input_wrapper");
        Expr input_cxyb = input_(c, x, y, b);
        if (!use_8bit_multiply(target)) {
            input_cxyb = i16(input_cxyb) - i16(input_zero_);
        }
        input(c, x, y, b) = input_cxyb;

        // Align the reduction loop of filter.
        const int vector_reduction = get_vector_reduction_factor(target, UInt(8));
        const int unroll_reduction = std::max<int>(vector_reduction, unroll_reduction_);
        const int accum_vector_size = natural_vector_size<int32_t>();

        // Set up the reduction loop and inputs.
        Expr filter_depth = filter_.dim(0).extent() * filter_.dim(2).extent();
        Expr filter_width = filter_.dim(4).extent();
        Expr filter_height = filter_.dim(5).extent();
        // Align the filter depth, which requires padding the input.
        filter_depth = align_up(filter_depth, unroll_reduction);
        RDom r(0, filter_width, 0, filter_height, 0, filter_depth);
        Expr filter_rdxyc =
            filter_(r.z % vector_reduction, c % accum_vector_size, r.z / vector_reduction, c / accum_vector_size, r.x, r.y);
        Expr input_rdxyc =
            input(r.z, x * stride_x_ + r.x * dilation_x_, y * stride_y_ + r.y * dilation_y_, b);

        Func offset_c("offset_c");
        Func sum_input("sum_input");
        Func convolved("convolved");
        if (use_8bit_multiply(target)) {
            // We want to compute the reduction:
            // convolved(c, x, y, b) = bias_(c)
            // convolved(c, x, y, b) +=
            //    (i32(input_rdxyc) - i32(input_zero_)) *
            //    (i32(filter_rdxyc) - i32(filter_zero_))
            //
            // However, this precludes using efficient dot product instructions. To
            // fix this, expand the expression:
            //
            // convolved(c, x, y, b) = bias_(c)
            // convolved(c, x, y, b) +=
            //    i32(filter_rdxyc) * i32(input_rdxyc) -
            //    i32(filter_rdxyc) * i32(input_zero_) -
            //    i32(filter_zero_) * i32(input_rdxyc) +
            //    i32(filter_zero_) * i32(input_zero_)
            //
            // We can then separate this into several reductions. First, the terms that
            // depend only on c.
            Expr r_size = filter_width * filter_height * filter_depth;
            // We need the negative of this reduction, so compute the sum first, and then
            // subtract it after.
            offset_c(c) += i32(u16(filter_rdxyc) * u16(input_zero_));
            offset_c(c) =
                bias_(c) + i32(u16(filter_zero_) * u16(input_zero_)) * r_size - offset_c(c);

            // The sum of the input is used to compute the filter_zero * input term.
            // TODO: This is separable, but a bit messy to optimize this way.
            sum_input(x, y, b) += i32(input_rdxyc);

            // Finally, the terms that depend on all of c, x, y, b.
            convolved(c, x, y, b) = offset_c(c) - i32(filter_zero_) * sum_input(x, y, b);
        } else {
            // Without 8-bit widening multiplies, we already subtracted the offsets,
            // and just have a single reduction of 16-bit multiplies to compute.
            convolved(c, x, y, b) = bias_(c);
        }
        convolved(c, x, y, b) += i32(input_rdxyc) * i32(filter_rdxyc);

        // Saturate and narrow the output.
        Expr output;
        if (output_.type() == halide_type_of<uint8_t>()) {
            output = quantize_and_relu_u8(convolved(c, x, y, b), output_multiplier_, output_shift_, output_zero_,
                                          output_min_, output_max_, target);
        } else {
            output = quantize_i16(convolved(c, x, y, b), output_multiplier_, output_shift_, target);
        }
        output_(c, x, y, b) = output;

        // Schedule
        interpret_as_tensor(input_);
        interpret_as_tensor(bias_);
        interpret_as_tensor(output_);
        require_same_min_extent(3, input_, output_);
        require_same_min_extent(0, bias_, output_);

        const int filter_alignment = vector_reduction * accum_vector_size;
        filter_.set_host_alignment(filter_alignment * filter_.type().bytes());
        filter_.dim(0).set_min(0).set_extent(vector_reduction).set_stride(1);
        filter_.dim(1).set_min(0).set_extent(accum_vector_size).set_stride(vector_reduction);
        filter_.dim(2).set_min(0).set_stride(filter_alignment);
        for (int d = 3; d < filter_.dimensions(); d++) {
            filter_.dim(d).set_min(0).set_stride(align(filter_.dim(d).stride(), filter_alignment));
        }

        const int input_alignment = unroll_reduction;
        input_.set_host_alignment(input_alignment);
        input_.dim(0).set_min(0).set_extent(filter_depth);
        for (int d = 1; d < input_.dimensions(); d++) {
            input_.dim(d).set_stride(align(input_.dim(d).stride(), input_alignment));
        }

        output_.compute_root();

        // Figure out how big the tiles we should optimize for should be by getting
        // the total number of accumulators best for this target and figuring out
        // tile sizes.
        const int accumulators = get_accumulator_count(target);
        std::vector<std::pair<int, int>> tile_sizes;
        const int min_tile_c = 1;
        const int max_tile_c = 4;
        for (int tile_c = max_tile_c; tile_c >= min_tile_c; tile_c /= 2) {
            int tile_x = std::min(8, accumulators / tile_c);
            tile_sizes.emplace_back(tile_c, tile_x);
        }
        tile_sizes.emplace_back(max_tile_c, 1);

        // We need to tile the output, but we can't use GuardWithIf because we need
        // things computed at the tile to have constant size. We can't assume the
        // output is bigger than a minimum size. So, we specialize for decreasing
        // tile sizes, and have a degenerate tile case to handle the rest.
        Var xo("xo");
        Expr output_channels = output_.dim(0).extent();
        Expr output_width = output_.dim(1).extent();
        for (auto i : tile_sizes) {
            const int tile_c = i.first;
            const int tile_x = i.second;
            output_
                .specialize(output_channels % (tile_c * accum_vector_size) == 0 && output_width >= tile_x)
                .split(c, co, c, tile_c * accum_vector_size, TailStrategy::RoundUp)
                .split(x, xo, x, tile_x, TailStrategy::ShiftInwards)
                .reorder(x, c, co, xo, y, b)
                .vectorize(c)
                .unroll(x);
        }

        // In case there are no suitable tile sizes, just make a dummy split so the
        // rest of the schedule still works.
        output_
            .split(c, co, c, accum_vector_size * min_tile_c, TailStrategy::PredicateStores)
            .split(x, xo, x, 1)
            .reorder(c, x, co, xo, y, b)
            .vectorize(c);

        // These GuardWithIf splits simplify for the constant-tile specializations,
        // but probably generate poor code for the general case.
        convolved.compute_at(output_, co)
            .store_in(MemoryType::Stack)
            .reorder(x, c)
            .vectorize(c, accum_vector_size * min_tile_c, TailStrategy::RoundUp)
            .unroll(c, max_tile_c, TailStrategy::GuardWithIf)
            .unroll(x);

        if (use_8bit_multiply(target)) {
            // Specialize this to avoid computing sum_input when it isn't needed.
            convolved.specialize(filter_zero_ == 0);
        }

        RVar rco, rci;
        convolved.update()
            .split(r.z, rco, rci, unroll_reduction)
            .reorder(rci, c, x, rco, r.x, r.y)
            .vectorize(c, accum_vector_size, TailStrategy::RoundUp)
            .unroll(c, max_tile_c, TailStrategy::GuardWithIf)
            .atomic()
            .vectorize(rci, vector_reduction)
            .unroll(rci)
            .unroll(x);
        if (unroll_reduction == vector_reduction) {
            // TODO: We used to not need this, but currently, it is a massive
            // savings (e.g. first conv layer of mobilenet drops from 760us to
            // 540us on ARM, at some point it was 560us on ARM without this).
            convolved.update().specialize(filter_depth == vector_reduction);
        }

        if (!use_8bit_multiply(target) && get_target().arch == Target::X86) {
            // On x86, widening subtracts eat up a lot of the already scarce
            // registers, so precomputing this outside the inner loop helps
            // a lot.
            // TODO: Maybe we should do this in a separate op. We already pad it
            // separately, we just don't dequantize it to 16-bit.
            input.compute_at(output_, y)
                .reorder(c, x);

            input.specialize(is_interleaved(input_, 4))
                .vectorize(c, 4, TailStrategy::RoundUp)
                .vectorize(x, natural_vector_size<int32_t>(), TailStrategy::GuardWithIf);

            Expr input_channels = input_.dim(0).extent();
            for (int i = natural_vector_size<int16_t>(); i >= unroll_reduction; i /= 2) {
                // Use GuardWithIf here to avoid growing the bounds.
                input.specialize(input_channels >= i)
                    .vectorize(c, i, TailStrategy::GuardWithIf);
            }
        } else if (unroll_reduction >= natural_vector_size<uint8_t>()) {
            // If we're unrolling a full vector's worth of reduction from the
            // input, explicitly load a vector of it first. This enables targeting
            // broadcasting dot products, like ARM's udot.
            input.in(convolved)
                .compute_at(convolved, c)
                .bound_extent(c, unroll_reduction)
                .vectorize(c);
        }

        if (use_8bit_multiply(target)) {
            // Precompute the channel offset at root.
            // TODO: This gets recomputed often when the op is split up into small
            // pieces.
            offset_c.compute_root()
                .vectorize(c, accum_vector_size, TailStrategy::RoundUp);
            offset_c.update(0)
                .specialize(input_zero_ != 0)
                .split(r.z, rco, rci, unroll_reduction)
                .split(c, co, c, accum_vector_size, TailStrategy::RoundUp)
                .reorder(rci, c, rco, r.x, r.y, co)
                .atomic()
                .vectorize(rci, vector_reduction)
                .unroll(rci)
                .vectorize(c);
            offset_c.update(1)
                .vectorize(c, accum_vector_size, TailStrategy::RoundUp);

            // Compute the sum of the input outside the loops over channels.
            sum_input.compute_at(output_, xo)
                .vectorize(x)
                .update()
                .split(r.z, rco, rci, unroll_reduction)
                .reorder(rci, x, rco, r.x, r.y)
                .atomic()
                .vectorize(rci)
                .vectorize(x)
                .specialize(stride_x_ == 1 && filter_depth == unroll_reduction && is_interleaved(input_, unroll_reduction));
        }

        // TODO: Pad this outside and let it constant fold.
        bias_.in().compute_root().store_in(MemoryType::Stack);
    }
};

// The above generator expects the filter to already be tiled into
class TileConvFilter : public Generator<TileConvFilter> {
public:
    Input<Buffer<uint8_t, 4>> input_{"input"};
    Input<uint8_t> input_zero_{"input_zero"};
    Input<uint8_t> output_zero_{"output_zero"};

    // 6D array of filter coefficients indexed by ci % n, co % k, ci / n, co / k, x, y,
    // where n = vector_reduction and k = accum_vector_size (below).
    Output<Buffer<void, 6>> output_{"output"};

    void configure() {
        if (use_8bit_multiply(target)) {
            output_.set_type(UInt(8));
        } else {
            output_.set_type(Int(16));
        }
    }

    void generate() {
        Func input_bounded = constant_exterior(input_, input_zero_);

        const int vector_reduction = get_vector_reduction_factor(target, UInt(8));
        const int vector_tile = natural_vector_size<int32_t>();

        Var bi("bi"), bo("bo");

        Expr filter_cxyb =
            i16(input_bounded(co * vector_reduction + ci, x, y, bo * vector_tile + bi)) - i16(input_zero_);
        output_(ci, bi, co, bo, x, y) = cast(output_.type(), filter_cxyb + output_zero_);

        // Schedule.
        output_.dim(0).set_min(0).set_extent(vector_reduction);
        output_.dim(1).set_min(0).set_extent(vector_tile).set_stride(vector_reduction);
        output_.dim(2).set_min(0).set_stride(vector_tile * vector_reduction);

        // TODO: We probably don't care about the performance of this, but if we do,
        // we could optimize this more.
        output_
            .compute_root()
            .reorder(ci, bi, bo, x, y, co)
            .vectorize(ci);
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::Conv, Conv)
HALIDE_REGISTER_GENERATOR(hannk::TileConvFilter, TileConvFilter)
