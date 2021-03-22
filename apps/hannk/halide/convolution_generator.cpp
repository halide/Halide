#include "Halide.h"
#include "common_halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;
using namespace Halide::ConciseCasts;

namespace hannk {

int get_vector_reduction_factor(const Target &target, Type t) {
    if (target.has_feature(Target::ARMDotProd)) {
        // ARM dot products can do 4-way reductions.
        return 4;
    }
    if (target.arch == Target::Hexagon) {
        // Hexagon can reduce 32-bits of inputs at once.
        return 32 / t.bits();
    }

    // Most targets can do 2-way horizontal reductions well.
    return 2;
}

int get_recommended_accumulators(const Target &target) {
    if (target.has_feature(Target::AVX512_Skylake) ||
        (target.arch == Target::ARM && target.bits == 64)) {
        // 32 registers total.
        return 24;
    } else {
        // 16 registers total.
        return 8;
    }
}

int smaller_power_of_two(int x) {
    int log2_next = std::lround(std::ceil(std::log2(x) - 1.0f));
    if (log2_next >= 0) {
        return 1 << log2_next;
    } else {
        return 0;
    }
}

class Convolution : public Generator<Convolution> {
public:
    // Unsigned 8-bit input tensor, indexed by input_depth, input_x, input_y,
    // input_batch.
    Input<Buffer<uint8_t>> input_{"input", 4};

    // A 4D array of 8-bit filter coefficients indexed by filter_depth, filter_x,
    // filter_y, filter_batch (aka. output_depth).
    Input<Buffer<uint8_t>> filter_{"filter", 4};

    // A 1D array of 32-bit biases. The bias should be added to the c
    // dimension of the output (i.e., # filter batches).
    Input<Buffer<int32_t>> bias_{"bias", 1};

    // Offsets for the input and filter.
    Input<uint8_t> input_offset_{"input_offset"};
    Input<uint8_t> filter_offset_{"filter_offset"};

    // The stride specifies how the input [x, y] is sub-subsampled. For every
    // spatial location [x, y] in the output buffer, the input buffer is sampled
    // spatially at [x * stride, y * stride]. The caller is responsible for
    // allocating the correct output memory.
    Input<int> stride_x_{"stride_x", 1, 1, 4};
    Input<int> stride_y_{"stride_y", 1, 1, 4};
    Input<int> dilation_x_{"dilation_x", 1, 1, 4};
    Input<int> dilation_y_{"dilation_y", 1, 1, 4};

    // Parameters for pointwise operations on the output.
    Input<int32_t> output_multiplier_{"output_multiplier"};
    Input<uint32_t> output_shift_{"output_shift"};
    Input<uint8_t> output_offset_{"output_offset"};
    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Input<int> guid_{"guid"};

    Output<Buffer<uint8_t>> output_{"output", 4};

    void generate() {
        // The algorithm.

        // Some free variables, where x and y represent the spatial dimensions.
        Var x("x"), y("y"), c("c"), b("b");

        // There are two codepaths below. On targets with widening 8-bit
        // multiplies, we implement the reduction by expanding the subtraction
        // of the offsets into 4 reductions involving 8-bit multiplies.
        // On targets without widening 8-bit multiplication, it's faster
        // to just subtract the offsets and use 16-bit multiplications.
        const bool use_8bit_multiply =
            get_target().arch != Target::X86 || get_target().has_feature(Target::AVX512_SapphireRapids);

        // Add a "zero" boundary condition to the input.
        Func input_bounded("input_bounded");
        Expr input_cxyb = constant_exterior(input_, input_offset_)(c, x, y, b);
        if (!use_8bit_multiply) {
            input_cxyb = i16(input_cxyb) - i16(input_offset_);
        }
        input_bounded(c, x, y, b) = input_cxyb;
        // And to c of the filter. This lets us align the inner reduction loop
        // however we want.
        Func filter_bounded =
            constant_exterior(filter_, filter_offset_, {{filter_.dim(0).min(), filter_.dim(0).extent()}});
        Func bias_bounded = repeat_edge(bias_);

        // Align the reduction loop of filter.
        int vector_reduction = get_vector_reduction_factor(get_target(), UInt(8));
        int unroll_reduction = std::max(4, vector_reduction);

        // Create a wrapper of the filter that we can reorder the storage of to be
        // more convenient for the inner loop.
        Var ci("ci"), co("co");
        Func filter_tiled("filter_tiled");
        Expr filter_cxyb =
            memoize_tag(filter_bounded(co * vector_reduction + ci, x, y, c), guid_);
        if (!use_8bit_multiply) {
            filter_cxyb = i16(filter_cxyb) - i16(filter_offset_);
        }
        filter_tiled(ci, co, x, y, c) = filter_cxyb;

        // Set up the reduction loop and inputs.
        filter_.dim(0).set_min(0);
        filter_.dim(1).set_min(0);
        filter_.dim(2).set_min(0);
        Expr filter_depth = filter_.dim(0).extent();
        Expr filter_width = filter_.dim(1).extent();
        Expr filter_height = filter_.dim(2).extent();
        // Align the filter depth, which requires padding the input.
        filter_depth =
            ((filter_depth + unroll_reduction - 1) / unroll_reduction) * unroll_reduction;
        RDom r(0, filter_width, 0, filter_height, 0, filter_depth);
        Expr filter_rdxyc =
            filter_tiled(r.z % vector_reduction, r.z / vector_reduction, r.x, r.y, c);
        Expr input_rdxyc =
            input_bounded(r.z, x * stride_x_ + r.x * dilation_x_, y * stride_y_ + r.y * dilation_y_, b);

        Func offset_c("offset_c");
        Func sum_input("sum_input");
        Func convolved("convolved");
        if (use_8bit_multiply) {
            // We want to compute the reduction:
            // convolved(c, x, y, b) = bias_(c)
            // convolved(c, x, y, b) +=
            //    (i32(input_rdxyc) - i32(input_offset_)) *
            //    (i32(filter_rdxyc) - i32(filter_offset_))
            //
            // However, this precludes using efficient dot product instructions. To
            // fix this, expand the expression:
            //
            // convolved(c, x, y, b) = bias_(c)
            // convolved(c, x, y, b) +=
            //    i32(filter_rdxyc) * i32(input_rdxyc) -
            //    i32(filter_rdxyc) * i32(input_offset_) -
            //    i32(filter_offset_) * i32(input_rdxyc) +
            //    i32(filter_offset_) * i32(input_offset_)
            //
            // We can then separate this into several reductions. First, the terms that
            // depend only on c.
            Expr r_size = filter_width * filter_height * filter_depth;
            // We need the negative of this reduction, so compute the sum first, and then
            // subtract it after.
            offset_c(c) += i32(filter_rdxyc) * i32(input_offset_);
            offset_c(c) =
                bias_bounded(c) + i32(filter_offset_) * i32(input_offset_) * r_size - offset_c(c);

            // The sum of the input is used to compute the filter_offset * input term.
            // TODO: This is separable, but a bit messy to optimize this way.
            sum_input(x, y, b) += i32(input_rdxyc);

            // Finally, the terms that depend on all of c, x, y, b.
            convolved(c, x, y, b) = offset_c(c) - i32(filter_offset_) * sum_input(x, y, b);
            convolved(c, x, y, b) += i32(filter_rdxyc) * i32(input_rdxyc);
        } else {
            // Without 8-bit widening multiplies, we already subtracted the offsets,
            // and just have a single reduction of 16-bit multiplies to compute.
            convolved(c, x, y, b) = bias_bounded(c);
            convolved(c, x, y, b) += i32(filter_rdxyc) * i32(input_rdxyc);
        }

        // Saturate and narrow the output.
        Expr output =
            multiply_quantized(convolved(c, x, y, b), output_multiplier_, output_shift_);
        output = i16_sat(output);
        output = saturating_add(output, output_offset_);
        output_(c, x, y, b) = clamp(u8_sat(output), output_min_, output_max_);

        // Schedule
        interpret_as_tensor(input_);
        interpret_as_tensor(filter_);
        interpret_as_tensor(bias_);
        interpret_as_tensor(output_);
        require_same_min_extent(3, input_, output_);
        require_same_min_extent(0, filter_, input_);
        require_same_min_extent(3, filter_, 0, output_);
        require_same_min_extent(0, bias_, output_);

        output_.compute_root();

        // Figure out how big the tiles we should optimize for should be by getting
        // the total number of accumulators best for this target and figuring out
        // tile sizes.
        const int accumulators = get_recommended_accumulators(get_target());
        const int tile_x = 4;
        std::vector<std::pair<int, int>> tile_sizes;
        for (int tile_c = accumulators / tile_x; tile_c >= 1;
             tile_c = smaller_power_of_two(tile_c)) {
            tile_sizes.emplace_back(tile_c, tile_x);
        }
        tile_sizes.emplace_back(4, 1);

        // We need to tile the output, but we can't use GuardWithIf because we need
        // things computed at the tile to have constant size. We can't assume the
        // output is bigger than a minimum size. So, we specialize for decreasing
        // tile sizes, and have a degenerate tile case to handle the rest.
        const int accum_vector_size = natural_vector_size<int32_t>();
        Var xo("xo");
        Expr output_channels = output_.dim(0).extent();
        Expr output_width = output_.dim(1).extent();
        for (auto i : tile_sizes) {
            int tile_c = i.first;
            int tile_x = i.second;
            output_
                .specialize(output_channels >= tile_c * accum_vector_size && output_width >= tile_x)
                .tile(c, x, co, xo, c, x, tile_c * accum_vector_size, tile_x, TailStrategy::ShiftInwards)
                .reorder(x, c, co, xo, y, b)
                .vectorize(c)
                .unroll(x);
        }

        // In case there are no suitable tile sizes, just make a dummy split so the
        // rest of the schedule still works.
        output_
            .tile(c, x, co, xo, c, x, accum_vector_size, 1, TailStrategy::GuardWithIf)
            .reorder(c, x, co, xo, y, b)
            .vectorize(c);

        // These GuardWithIf splits simplify for the constant-tile specializations,
        // but probably generate poor code for the general case.
        convolved.compute_at(output_, co)
            .store_in(MemoryType::Stack)
            .reorder(x, c, y, b)
            .vectorize(c, accum_vector_size, TailStrategy::RoundUp)
            .unroll(c, 4, TailStrategy::GuardWithIf)
            .unroll(x);

        if (use_8bit_multiply) {
            // Specialize this to avoid computing sum_input when it isn't needed.
            convolved.specialize(filter_offset_ == 0);
        }

        RVar rco, rci;
        convolved.update()
            .split(r.z, rco, rci, unroll_reduction)
            .reorder(rci, x, c, rco, r.x, r.y, y, b)
            .vectorize(c, accum_vector_size, TailStrategy::RoundUp)
            .unroll(c, 4, TailStrategy::GuardWithIf)
            .atomic()
            .vectorize(rci, vector_reduction)
            .unroll(rci)
            .unroll(x);

        if (use_8bit_multiply) {
            // Precompute the channel offset at root.
            // TODO: This gets recomputed often when the op is split up into small
            // pieces.
            offset_c.compute_root();
            offset_c.update(0)
                .specialize(input_offset_ != 0)
                .split(r.z, rco, rci, unroll_reduction)
                .reorder(rci, c, rco, r.x, r.y)
                .atomic()
                .vectorize(rci, vector_reduction)
                .unroll(rci)
                .vectorize(c, accum_vector_size, TailStrategy::RoundUp);
            offset_c.update(1)
                .vectorize(c, accum_vector_size, TailStrategy::RoundUp);

            // Compute the sum of the input outside the loops over channels.
            sum_input.in().compute_at(output_, y)
                .vectorize(x, accum_vector_size, TailStrategy::RoundUp);
            sum_input.compute_at(sum_input.in(), x)
                .vectorize(x)
                .update()
                .reorder(r.z, r.x, r.y, x)
                .atomic()
                .vectorize(r.z, unroll_reduction)
                .vectorize(x);
        }

        // TODO: We often don't need the boundary condition on the input,
        // and it's expensive.
        input_bounded.compute_at(output_, y)
            .store_in(MemoryType::Stack)
            .reorder(c, x, y);

        // For 3-channel interleaved inputs, we need to try to use
        // interleaving loads/stores when available.
        input_bounded.specialize(input_.dim(0).extent() == 3 && input_.dim(1).stride() == 3)
            .unroll(c)
            .vectorize(x, natural_vector_size<uint8_t>(), TailStrategy::RoundUp);

        // TODO: This is a mess. We need a better way to implement a
        // straightforward boundary condition.
        int vector_size_input =
            use_8bit_multiply ? natural_vector_size<uint8_t>() : natural_vector_size<int16_t>();
        for (int i = vector_size_input; i >= 4; i /= 2) {
            // Use GuardWithIf here to avoid growing the bounds.
            input_bounded.specialize(input_.dim(0).extent() >= i)
                .vectorize(c, i, TailStrategy::GuardWithIf);
        }

        bias_bounded.compute_root()
            .store_in(MemoryType::Stack);

        // Pretranspose the filter, so we don't need to do it in the inner loop.
        // TODO: This gets recomputed often when the op is split up into small
        // pieces.
        filter_tiled
            .compute_root()
            .memoize()
            .reorder_storage(ci, c, co, x, y)
            .reorder(ci, c, x, y, co)
            .bound(ci, 0, vector_reduction)
            .align_storage(ci, vector_reduction)
            .vectorize(ci);
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::Convolution, Convolution)
