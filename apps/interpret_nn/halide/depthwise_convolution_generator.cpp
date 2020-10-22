#include "Halide.h"
#include "common_halide.h"

namespace interpret_nn {

using Halide::Generator;
using Halide::ConciseCasts::i16;
using Halide::ConciseCasts::i16_sat;
using Halide::ConciseCasts::i32;
using Halide::ConciseCasts::u8_sat;

class DepthwiseConvolution : public Generator<DepthwiseConvolution> {
public:
    // If true, the input is assumed to have one channel, and it is broadcasted.
    GeneratorParam<bool> broadcast_channels_{"broadcast_channels", false};

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

        // Pad x and y with the value that produces zero after the input offset is
        // subtracted.
        Func input_bounded = ConstantExteriorTensor(input_, input_offset_);

        // Apply the c multiplier.
        Func resampled_input("resampled_input");
        Expr c_resampled = broadcast_channels_ ? 0 : c / depth_multiplier_;
        resampled_input(c, x, y, b) = input_bounded(c_resampled, x, y, b);

        Func filter_biased("filter_biased");
        Func input_biased("input_biased");
        filter_biased(c, x, y) = i16(filter_(c, x, y)) - i16(filter_offset_);
        input_biased(c, x, y, b) =
            i16(resampled_input(c, x, y, b)) - i16(input_offset_);

        // Do the convolution in 32-bit.
        Func convolved("convolved");
        convolved(c, x, y, b) = bias_(c);
        filter_.dim(1).set_min(0);
        filter_.dim(2).set_min(0);
        Expr filter_width = filter_.dim(1).extent();
        Expr filter_height = filter_.dim(2).extent();
        RDom r(0, filter_width, 0, filter_height);
        Expr filter_drxy = filter_biased(c, r.x, r.y);
        Expr input_drxyb = input_biased(c, x * stride_x_ + r.x * dilation_x_,
                                        y * stride_y_ + r.y * dilation_y_, b);
        convolved(c, x, y, b) += i32(filter_drxy) * i32(input_drxyb);

        // Saturate and narrow the output.
        Expr output =
            MultiplyByQuantizedMultiplierSmallerThanOne(convolved(c, x, y, b), output_multiplier_,
                                                        output_shift_) +
            output_offset_;
        output_(c, x, y, b) = clamp(u8_sat(output), output_min_, output_max_);

        // Schedule.
        InterpretAsTensor(input_);
        InterpretAsTensor(filter_);
        InterpretAsTensor(bias_);
        InterpretAsTensor(output_);

        if (broadcast_channels_) {
            // When we're broadcasting input channels, require that the input has only
            // one channel.
            input_.dim(0).set_extent(1);
        }

        const int vector_size = natural_vector_size<uint8_t>();

        // Tile the output, so we can try to re-use loads spatially when performing
        // convolution. This also helps because we can schedule the input and not
        // waste work for stride < kTileSize.
        const int kTileSize = 2;
        Var xo("xo"), yo("yo"), co("co");
        Expr output_channels = output_.dim(0).extent();
        output_.compute_root()
            .tile(x, y, xo, yo, x, y, kTileSize, kTileSize,
                  TailStrategy::ShiftInwards)
            .reorder(x, y, c, xo, yo, b)
            .unroll(x)
            .unroll(y)
            .specialize(output_channels >= vector_size)
            .split(c, co, c, vector_size, TailStrategy::ShiftInwards)
            .reorder(x, y, c, xo, co, yo, b)
            .vectorize(c);

        output_.split(c, co, c, output_channels, TailStrategy::RoundUp)
            .reorder(x, y, c, xo, co, yo, b);

        convolved.compute_at(output_, xo)
            .store_in(MemoryType::Stack)
            .bound_extent(c, vector_size)
            .reorder(x, y, c, b)
            .unroll(x)
            .unroll(y)
            .vectorize(c);
        convolved.update()
            .reorder(x, y, c, r.x, r.y, b)
            .unroll(x)
            .unroll(y)
            .vectorize(c);
        convolved.update()
            .specialize(filter_width == 3 && filter_height == 3)
            .unroll(r.x);

        // TODO: This gets recomputed often when the op is split up into small
        // pieces.
        filter_biased.compute_root();

        // The reason broadcast_channels_ is a GeneratorParam and not a
        // specialization is that we can't specialize the (lack of) compute_at here.
        if (!broadcast_channels_) {
            resampled_input
                .compute_at(output_, co)
                .store_in(MemoryType::Stack)
                .vectorize(c, vector_size, TailStrategy::GuardWithIf);

            for (int dm : {1, 3}) {
                resampled_input.specialize(depth_multiplier_ == dm);
            }
        }
    }
};

}  // namespace interpret_nn

HALIDE_REGISTER_GENERATOR(interpret_nn::DepthwiseConvolution,
                          DepthwiseConvolution)
