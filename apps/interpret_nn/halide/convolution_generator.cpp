#include "Halide.h"
#include "common_halide.h"

namespace interpret_nn {

using Halide::_0;
using Halide::_1;
using Halide::_2;
using Halide::_3;
using Halide::Generator;
using Halide::Target;
using Halide::Type;
using Halide::BoundaryConditions::constant_exterior;
using Halide::ConciseCasts::i16;
using Halide::ConciseCasts::i16_sat;
using Halide::ConciseCasts::i32;
using Halide::ConciseCasts::u32;
using Halide::ConciseCasts::u8_sat;

int GetVectorReduction(const Target &target, Type t) {
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

int GetRecommendedAccumulators(const Target &target) {
    if (target.has_feature(Target::AVX512_Skylake) ||
        (target.arch == Target::ARM && target.bits == 64)) {
        // 32 registers total.
        return 24;
    } else {
        // 16 registers total.
        return 16;
    }
}

int SmallerPowerOfTwo(int x) {
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
    Input<int> output_multiplier_{"output_multiplier"};
    Input<int> output_shift_{"output_shift"};
    Input<uint8_t> output_offset_{"output_offset"};
    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Output<Buffer<uint8_t>> output_{"output", 4};

    void generate() {
        // The algorithm.

        // Some free variables, where x and y represent the spatial dimensions.
        Var x("x"), y("y"), c("c"), b("b");

        // Add a "zero" boundary condition to x and y dimensions of the input.
        Func input_bounded = ConstantExteriorTensor(input_, input_offset_);
        // And to c of the filter. This lets us align the inner reduction loop
        // however we want.
        Func filter_bounded =
            constant_exterior(filter_, filter_offset_,
                              {{filter_.dim(0).min(), filter_.dim(0).extent()}});

        // Align the reduction loop of filter.
        int vector_reduction = GetVectorReduction(get_target(), UInt(8));

        // Create a wrapper of the filter that we can reorder the storage of to be
        // more convenient for the inner loop.
        Var ci("ci"), co("co");
        Func filter_tiled("filter_tiled");
        filter_tiled(ci, co, x, y, c) =
            filter_bounded(co * vector_reduction + ci, x, y, c);

        // Set up the reduction loop and inputs.
        filter_.dim(0).set_min(0);
        filter_.dim(1).set_min(0);
        filter_.dim(2).set_min(0);
        Expr filter_depth = filter_.dim(0).extent();
        Expr filter_width = filter_.dim(1).extent();
        Expr filter_height = filter_.dim(2).extent();
        // Align the filter depth, which requires padding the input.
        filter_depth =
            ((filter_depth + vector_reduction - 1) / vector_reduction) * vector_reduction;
        RDom r(0, filter_width, 0, filter_height, 0, filter_depth);
        Expr filter_rdxyc =
            filter_tiled(r.z % vector_reduction, r.z / vector_reduction, r.x, r.y, c);
        Expr input_rdxyc = input_bounded(r.z, x * stride_x_ + r.x * dilation_x_,
                                         y * stride_y_ + r.y * dilation_y_, b);

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
        Func offset_c("offset_c");
        Expr r_size = filter_width * filter_height * filter_depth;
        // We need the negative of this reduction, so compute the sum first, and then
        // subtract it after.
        offset_c(c) += i32(filter_rdxyc) * i32(input_offset_);
        offset_c(c) =
            bias_(c) + i32(filter_offset_) * i32(input_offset_) * r_size - offset_c(c);

        // The sum of the input is used to compute the filter_offset * input term.
        // TODO: This is separable, but a bit messy to optimize this way.
        Func sum_input("sum_input");
        sum_input(x, y, b) += i32(input_rdxyc);

        // Finally, the terms that depend on all of c, x, y, b.
        Func convolved("convolved");
        convolved(c, x, y, b) = offset_c(c) - i32(filter_offset_) * sum_input(x, y, b);
        convolved(c, x, y, b) += i32(filter_rdxyc) * i32(input_rdxyc);

        // Saturate and narrow the output.
        Expr output =
            MultiplyByQuantizedMultiplierSmallerThanOne(convolved(c, x, y, b),
                                                        output_multiplier_,
                                                        output_shift_) +
            output_offset_;
        output_(c, x, y, b) = clamp(u8_sat(output), output_min_, output_max_);

        // Schedule
        InterpretAsTensor(input_);
        InterpretAsTensor(filter_);
        InterpretAsTensor(bias_);
        InterpretAsTensor(output_);

        output_.compute_root();

        // Figure out how big the tiles we should optimize for should be by getting
        // the total number of accumulators best for this target and figuring out
        // tile sizes.
        const int accumulators = GetRecommendedAccumulators(get_target());
        const int tile_x = 4;
        std::vector<std::pair<int, int>> tile_sizes;
        for (int tile_c = accumulators / tile_x; tile_c >= 1;
             tile_c = SmallerPowerOfTwo(tile_c)) {
            tile_sizes.emplace_back(tile_c, tile_x);
        }
        tile_sizes.emplace_back(4, 1);

        // We need to tile the output, but we can't use GuardWithIf because we need
        // things computed at the tile to have constant size. We can't assume the
        // output is bigger than a minimum size. So, we specialize for decreasing
        // tile sizes, and have a degenerate tile case to handle the rest.
        const int vector_size = natural_vector_size<uint8_t>() / vector_reduction;
        Var xo("xo");
        Expr output_channels = output_.dim(0).extent();
        Expr output_width = output_.dim(1).extent();
        for (auto i : tile_sizes) {
            int tile_c = i.first;
            int tile_x = i.second;
            output_
                .specialize(output_channels >= tile_c * vector_size &&
                            output_width >= tile_x)
                .tile(c, x, co, xo, c, x, tile_c * vector_size, tile_x,
                      TailStrategy::ShiftInwards)
                .reorder(c, x, co, xo, y, b)
                .vectorize(c, natural_vector_size<uint8_t>(), TailStrategy::GuardWithIf)
                .unroll(c);
        }

        // In case there are no suitable tile sizes, just make a dummy split so the
        // rest of the schedule still works.
        output_
            .tile(c, x, co, xo, c, x, 1, 1, TailStrategy::RoundUp)
            .reorder(c, x, co, xo, y, b);

        // These GuardWithIf splits simplify for the constant-tile specializations,
        // but probably generate poor code for the general case.
        convolved.compute_at(output_, co)
            .store_in(MemoryType::Stack)
            .reorder(x, c, y, b)
            .vectorize(c)
            .unroll(x);

        // Specialize this to avoid computing sum_input when it isn't needed.
        convolved.specialize(filter_offset_ == 0);

        RVar rco, rci;
        convolved.update()
            .split(r.z, rco, rci, vector_reduction)
            .reorder(rci, x, c, rco, r.x, r.y, y, b)
            .vectorize(c)
            .atomic()
            .vectorize(rci)
            .unroll(x);

        // Precompute the channel offset at root.
        // TODO: This gets recomputed often when the op is split up into small
        // pieces.
        offset_c.compute_root();
        offset_c.update(0)
            .specialize(input_offset_ != 0)
            .split(r.z, rco, rci, vector_reduction)
            .reorder(rci, c, rco, r.x, r.y)
            .atomic()
            .vectorize(rci, vector_reduction)
            .vectorize(c, vector_size, TailStrategy::GuardWithIf);
        offset_c.update(1)
            .vectorize(c, vector_size, TailStrategy::GuardWithIf);

        // Compute the sum of the input outside the loops over channels.
        sum_input.compute_at(output_, xo)
            .vectorize(x);
        sum_input.update()
            .reorder(x, r.z, r.x, r.y, y, b)
            .atomic()
            .vectorize(r.z, vector_size * vector_reduction, TailStrategy::GuardWithIf)
            .unroll(x);

        // TODO: We only need this (and the boundary condition on c) when
        // filter.dim(0).extent() % 4 != 0 :(
        input_bounded.compute_at(output_, y)
            .store_in(MemoryType::Stack)
            .reorder(x, y, b, c)
            .vectorize(c, vector_size, TailStrategy::GuardWithIf);

        // Pretranspose the filter, so we don't need to do it in the inner loop.
        // TODO: This gets recomputed often when the op is split up into small
        // pieces.
        filter_tiled.compute_root()
            .reorder_storage(ci, c, co, x, y)
            .reorder(ci, c, x, y, co)
            .bound(ci, 0, vector_reduction)
            .align_storage(ci, vector_reduction)
            .vectorize(ci);
    }
};

}  // namespace interpret_nn

HALIDE_REGISTER_GENERATOR(interpret_nn::Convolution, Convolution)
