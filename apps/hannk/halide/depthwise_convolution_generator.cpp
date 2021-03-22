#include "Halide.h"
#include "common_halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;
using namespace Halide::ConciseCasts;

namespace hannk {

class DepthwiseConvolution : public Generator<DepthwiseConvolution> {
public:
    // If positive, a constant inverse depth multiplier.
    GeneratorParam<int> inv_depth_multiplier_{"inv_depth_multiplier", -1};

    // Unsigned 8-bit input tensor, indexed by c, x, y, b.
    Input<Buffer<uint8_t>> input_{"input", 4};

    // A 3D array of 8-bit filter coefficients indexed by c, x, y.
    Input<Buffer<uint8_t>> filter_{"filter", 3};

    // A 1D array of 32-bit biases indexed by c.
    Input<Buffer<int32_t>> bias_{"bias", 1};

    // The c multiplier specifies the ratio between the output c and the
    // input c.
    Input<int> depth_multiplier_{"depth_multiplier"};

    // Offsets for the input and filter.
    Input<uint8_t> input_offset_{"input_offset"};
    Input<uint8_t> filter_offset_{"filter_offset"};

    // The stride specifies how the input [x, y] are sub-subsampled. For every
    // spatial location [x, y] in the output buffer, the input buffer is sampled
    // spatially at [x * stride, y * stride]. The caller should ensure that
    // [x * stride, y * stride] is a valid spatial location in the input buffer.
    // Generally, this means setting the output buffer's [width, height] to be
    // the input buffer's [width, height] / stride.
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

    Output<Buffer<uint8_t>> output_{"output", 4};

    void generate() {
        // The algorithm.

        // Some free variables, where x and y represent the spatial dimensions.
        Var x("x"), y("y"), c("c"), b("b");

        // Pad x and y with the value that produces zero after the input offset is
        // subtracted.
        Func input_bounded = constant_exterior(input_, input_offset_);

        Func filter_bounded = repeat_edge(filter_);
        Func bias_bounded = repeat_edge(bias_);

        // Apply the c multiplier.
        Func resampled_input("resampled_input");
        Expr c_resampled = inv_depth_multiplier_ >= 0 ? c * inv_depth_multiplier_ : c / depth_multiplier_;
        resampled_input(c, x, y, b) = input_bounded(c_resampled, x, y, b);

        Func filter_zeroed("filter_zeroed");
        Func input_zeroed("input_zeroed");
        filter_zeroed(c, x, y) = i16(filter_bounded(c, x, y)) - i16(filter_offset_);
        input_zeroed(c, x, y, b) = i16(resampled_input(c, x, y, b)) - i16(input_offset_);

        // Do the convolution in 32-bit.
        filter_.dim(1).set_min(0);
        filter_.dim(2).set_min(0);
        Expr filter_width = filter_.dim(1).extent();
        Expr filter_height = filter_.dim(2).extent();
        RDom r(0, filter_width, 0, filter_height);
        Expr filter_drxy = filter_zeroed(c, r.x, r.y);
        Expr input_drxyb =
            input_zeroed(c, x * stride_x_ + r.x * dilation_x_, y * stride_y_ + r.y * dilation_y_, b);
        Func convolved("convolved");
        convolved(c, x, y, b) = bias_bounded(c);
        convolved(c, x, y, b) += i32(filter_drxy) * i32(input_drxyb);

        // Saturate and narrow the output.
        Expr output =
            multiply_quantized(convolved(c, x, y, b), output_multiplier_, output_shift_);
        output = i16_sat(output);
        output = saturating_add(output, output_offset_);
        output_(c, x, y, b) = clamp(u8_sat(output), output_min_, output_max_);

        // Schedule.
        interpret_as_tensor(input_);
        interpret_as_tensor(filter_);
        interpret_as_tensor(bias_);
        interpret_as_tensor(output_);
        require_same_min_extent(3, input_, output_);
        require_same_min_extent(0, bias_, output_);
        require_same_min_extent(0, filter_, output_);

        if (inv_depth_multiplier_ < 0) {
            output_.dim(0).set_min(input_.dim(0).min() * depth_multiplier_);
            output_.dim(0).set_extent(input_.dim(0).extent() * depth_multiplier_);
        } else if (inv_depth_multiplier_ != 0) {
            input_.dim(0).set_min(output_.dim(0).min() * inv_depth_multiplier_);
            input_.dim(0).set_extent(output_.dim(0).extent() * inv_depth_multiplier_);
        } else {
            // When we're broadcasting input channels, require that the input has only
            // one channel.
            input_.dim(0).set_min(0).set_extent(1);
            input_.dim(1).set_stride(1);
        }

        const int vector_size = natural_vector_size<uint8_t>();

        // Tile the output, so we can try to re-use loads spatially when performing
        // convolution. This also helps because we can schedule the input and not
        // waste work for stride < kTileSize.
        // We split co and reorder it outermost, so we can maximize locality of the
        // filter. We even put it outside of the batch loop, so we can compute the
        // boundary condition on the filter at co and reuse it across batches.
        const int kTileSize = 2;
        Var xo("xo"), yo("yo"), co("co");
        Expr output_channels = output_.dim(0).extent();
        Expr output_width = output_.dim(1).extent();
        Expr output_height = output_.dim(2).extent();
        output_.compute_root()
            .specialize(output_width >= kTileSize && output_height >= kTileSize)
            .tile(x, y, xo, yo, x, y, kTileSize, kTileSize, TailStrategy::ShiftInwards)
            .unroll(x)
            .unroll(y)
            .split(c, co, c, vector_size, TailStrategy::GuardWithIf)
            .reorder(x, y, c, xo, yo, b, co)
            .vectorize(c);

        // Enable 1x1 outputs to work.
        output_
            .tile(x, y, xo, yo, x, y, 1, 1, TailStrategy::RoundUp)
            .unroll(x)
            .unroll(y)
            .split(c, co, c, vector_size, TailStrategy::GuardWithIf)
            .reorder(x, y, c, xo, yo, b, co)
            .vectorize(c);

        convolved.compute_at(output_, xo)
            .store_in(MemoryType::Register)
            .bound_extent(c, vector_size)
            .unroll(x)
            .unroll(y)
            .vectorize(c);
        convolved.update()
            .reorder(x, y, r.x, r.y)
            .unroll(x)
            .unroll(y)
            .vectorize(c);
        convolved.update()
            .specialize(filter_width == 3 && filter_height == 3)
            .reorder(r.x, x, y, r.y)
            .unroll(r.x);

        bias_bounded.compute_root();

        if (inv_depth_multiplier_ < 0) {
            // The reason inv_depth_multiplier_ is a GeneratorParam and not a
            // specialization is that we can't specialize the (lack of) compute_at here.
            resampled_input
                .compute_at(output_, b)
                .store_in(MemoryType::Stack)
                .vectorize(c, vector_size, TailStrategy::GuardWithIf);

            for (int dm : {1}) {
                resampled_input.specialize(depth_multiplier_ == dm);
            }
            resampled_input.specialize_fail("unsupported depth multiplier");
        } else if (inv_depth_multiplier_ == 0) {
            // For the broadcasting case, we want to pull the boundary condition out
            // of the inner loop before we broadcast the channels.
            input_bounded
                .compute_at(output_, b)
                .store_in(MemoryType::Stack)
                .vectorize(Halide::_1, vector_size, TailStrategy::RoundUp);
        }

        filter_bounded
            .compute_at(output_, co)
            .store_in(MemoryType::Stack)
            .align_storage(Halide::_0, vector_size)
            .specialize(output_channels >= vector_size)
            .vectorize(Halide::_0, vector_size);
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::DepthwiseConvolution,
                          DepthwiseConvolution)
