#include "Halide.h"
#include "halide/common_halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace hannk {

class DepthwiseConv : public Generator<DepthwiseConv> {
public:
    // This is used to compute ci = co * inv_depth_multiplier. There are
    // only 2 values that make sense here:
    // - inv_depth_multiplier = 1 => depth_multiplier = 1
    // - inv_depth_multiplier = 0 => broadcasting 1 channel of input
    GeneratorParam<int> inv_depth_multiplier_{"inv_depth_multiplier", 1};

    // When true, we assume the vector size is divided evenly by the number
    // of channels, and we use the input_stride_x parameter as the stride of
    // x of the input, instead of the x dimension of the buffer.
    GeneratorParam<bool> shallow_{"shallow", false};

    // Unsigned 8-bit input tensor, indexed by ci, x, y, b.
    Input<Buffer<uint8_t, 4>> input_{"input"};
    Input<uint8_t> input_zero_{"input_zero"};

    // A 3D array of 8-bit filter coefficients indexed by co, x, y.
    Input<Buffer<uint8_t, 3>> filter_{"filter"};
    Input<uint8_t> filter_zero_{"filter_zero"};

    // A 1D array of 32-bit biases indexed by co.
    Input<Buffer<int32_t, 1>> bias_{"bias"};

    // The stride specifies how the input [x, y] are sub-subsampled. For every
    // spatial location [x, y] in the output buffer, the input buffer is sampled
    // spatially at [x * stride, y * stride]. The caller should ensure that
    // [x * stride, y * stride] is a valid spatial location in the input buffer.
    // Generally, this means setting the output buffer's [width, height] to be
    // the input buffer's [width, height] / stride.
    Input<int> stride_x_{"stride_x"};
    Input<int> stride_y_{"stride_y"};
    Input<int> dilation_x_{"dilation_x"};
    Input<int> dilation_y_{"dilation_y"};

    // When c and x are fused, this is used to specify the stride of x of the input
    // within the fused c-x dimension.
    Input<int> input_stride_x_{"input_stride_x"};

    Input<int32_t> output_multiplier_{"output_multiplier"};
    Input<int32_t> output_shift_{"output_shift"};
    Input<uint8_t> output_zero_{"output_zero"};
    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Output<Buffer<uint8_t, 4>> output_{"output"};

    void generate() {
        // The algorithm.

        // For the shallow case, we need to know the vector size in the algorithm.
        int vector_size = natural_vector_size<uint8_t>();
        if (get_register_count(target) < 32) {
            vector_size = natural_vector_size<int16_t>();
        }

        // Some free variables, where x and y represent the spatial dimensions.
        Var x("x"), y("y"), c("c"), b("b");

        // Apply the c multiplier.
        Func resampled_input("resampled_input");
        resampled_input(c, x, y, b) = input_(c * inv_depth_multiplier_, x, y, b);

        Func filter_bounded("filter_bounded");
        Func bias_bounded("bias_bounded");
        Expr filter_c = c;
        if (shallow_) {
            // When the filter is shallow, we need a boundary condition on the
            // filter and bias.
            Expr filter_depth = filter_.dim(0).extent();
            filter_bounded(c, x, y) = filter_(c % filter_depth, x, y);
            bias_bounded(c) = bias_(c % filter_depth);

            // For shallow depthwise, we repeat the filter at multiples of the vector size.
            filter_c = c % vector_size;
        } else {
            filter_bounded(c, x, y) = filter_(c, x, y);
            bias_bounded(c) = bias_(c);
        }

        Func filter_zeroed("filter_zeroed");
        filter_zeroed(c, x, y) = i16(filter_bounded(c, x, y)) - i16(filter_zero_);

        // Do the convolution in 32-bit.
        filter_.dim(1).set_min(0);
        filter_.dim(2).set_min(0);
        Expr filter_width = filter_.dim(1).extent();
        Expr filter_height = filter_.dim(2).extent();
        RDom r(0, filter_width, 0, filter_height);
        Expr filter_zeroed_rdxy = filter_zeroed(filter_c, r.x, r.y);

        // We want to compute the reduction:
        // convolved(c, x, y, b) = bias(c)
        // convolved(c, x, y, b) +=
        //    i32(filter_zeroed_rdxy) *
        //    (i32(input_rdxy) - i32(input_zero_))
        //
        // However, this requires subtracting the input zero at every output.
        // We can factor the reduction like so:
        //
        // convolved(c, x, y, b) = bias(c)
        // convolved(c, x, y, b) +=
        //    i32(filter_zeroed_rdxy) * i32(input_rdxyc) -
        //    i32(filter_zeroed_rdxy) * i32(input_zero_)
        //
        // The latter reduction can be computed once per output channel.
        Func sum_filter("sum_filter");
        sum_filter(c) += i32(filter_zeroed_rdxy);

        Func offset_c("offset_c");
        offset_c(c) = bias_bounded(c) - sum_filter(c) * i32(input_zero_);

        Expr rx = x * stride_x_ + r.x * dilation_x_;
        Expr ry = y * stride_y_ + r.y * dilation_y_;
        Expr input_rdxy;
        if (shallow_) {
            input_rdxy = resampled_input(c + rx * input_stride_x_, 0, ry, b);
        } else {
            input_rdxy = resampled_input(c, rx, ry, b);
        }
        Func convolved("convolved");
        convolved(c, x, y, b) = offset_c(filter_c);
        convolved(c, x, y, b) += i32(filter_zeroed_rdxy) * i32(input_rdxy);

        output_(c, x, y, b) =
            quantize_and_relu_u8(convolved(c, x, y, b), output_multiplier_, output_shift_,
                                 output_zero_, output_min_, output_max_, target);

        // Schedule.
        interpret_as_tensor(input_);
        interpret_as_tensor(filter_);
        interpret_as_tensor(bias_);
        interpret_as_tensor(output_);
        require_same_min_extent(3, input_, output_);
        if (shallow_) {
            // Shallow inputs should have fused c and x, and left x as a dummy dim.
            output_.dim(1).set_min(0).set_extent(1);
        } else {
            require_same_min_extent(0, output_, bias_);
            require_same_min_extent(0, output_, filter_);
        }

        if (inv_depth_multiplier_ == 0) {
            // When we're broadcasting input channels, require that the input has only
            // one channel.
            input_.dim(0).set_extent(1);
        } else if (shallow_) {
            // Don't require alignment for shallow. We'd like to do so, but don't
            // have a good way to express it currently, since it requires
            // padding the fusion of two dimensions, and requiring alignment
            // will cause failures on wide-vector architectures like AVX512, HVX, etc.
            // We'll just pay the alignment penalty here for now.
        } else if (inv_depth_multiplier_ == 1) {
            // Require the input to be aligned.
            const int input_alignment = vector_size;
            input_.set_host_alignment(input_alignment);
            for (int d = 1; d < input_.dimensions(); d++) {
                input_.dim(d).set_stride(align(input_.dim(d).stride(), input_alignment));
            }
        }

        // Tile the output, so we can try to re-use loads spatially when performing
        // convolution. This also helps because we can schedule the input and not
        // waste work for strides less than the tile size.
        // We split co and reorder it outermost, so we can maximize locality of the
        // filter. We even put it outside of the batch loop, so we can compute the
        // boundary condition on the filter at co and reuse it across batches.
        const int kAccumulators = 4;
        const int kTileW = shallow_ ? 1 : 2;
        const int kTileH = kAccumulators / kTileW;
        // When the output is small, the overhead from shift inwards can be large.
        // Only tile when the input is at least this many tiles to avoid this.
        const int kMinTiles = 4;
        Var xo("xo"), yo("yo"), co("co");
        Expr output_width = output_.dim(1).extent();
        Expr output_height = output_.dim(2).extent();
        Expr use_tiles =
            (output_width >= kTileW * kMinTiles || output_width % kTileW == 0) &&
            (output_height >= kTileH * kMinTiles || output_height % kTileH == 0);
        output_.compute_root()
            .specialize(use_tiles)
            .tile(x, y, xo, yo, x, y, kTileW, kTileH, TailStrategy::ShiftInwards)
            .split(c, co, c, vector_size, TailStrategy::PredicateStores)
            .reorder(x, y, c, xo, yo, b, co)
            .unroll(x)
            .unroll(y)
            .vectorize(c);

        // In the general case, use dummy 1x1 tiles.
        output_
            .tile(x, y, xo, yo, x, y, 1, 1)
            .split(c, co, c, vector_size, TailStrategy::PredicateStores)
            .reorder(x, y, c, xo, yo, b, co)
            .unroll(x)
            .unroll(y)
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
            .unroll(r.x)
            .unroll(r.y);

        LoopLevel filter_compute_at = shallow_ ? LoopLevel::root() : LoopLevel(output_, co);

        // This doesn't read from any of the inputs directly, so we can vectorize
        // rounding up.
        offset_c.compute_at(filter_compute_at)
            .store_in(MemoryType::Stack)
            .vectorize(c, vector_size, TailStrategy::RoundUp);

        filter_zeroed.compute_at(filter_compute_at)
            .store_in(MemoryType::Stack)
            .align_storage(c, vector_size)
            .vectorize(c, vector_size, TailStrategy::PredicateLoads);

        bias_bounded.compute_at(filter_compute_at)
            .store_in(MemoryType::Stack)
            .vectorize(c, vector_size, TailStrategy::PredicateLoads);
    }
};

// A generator to resample the channels of a buffer. This is used to
// implement depth_multiplier != 1 for DepthwiseConv above if the
// depth_multiplier is too small to use the broadcasting version.
class UpsampleChannels : public Generator<UpsampleChannels> {
public:
    // Unsigned 8-bit input tensor, indexed by ci, x, y, b.
    Input<Buffer<uint8_t, 4>> input_{"input"};

    // The depth multiplier specifies the ratio between co and ci.
    Input<int> factor_{"factor"};

    // Unsigned 8-bit output tensor, indexed by co, x, y, b.
    Output<Buffer<uint8_t, 4>> output_{"output"};

    void generate() {
        Var x("x"), y("y"), c("c"), b("b");
        output_(c, x, y, b) = input_(c / factor_, x, y, b);

        require_same_min_extent(3, input_, output_);

        const int vector_size = natural_vector_size<uint8_t>();

        output_.compute_root()
            .vectorize(c, vector_size, TailStrategy::Predicate);

        output_.specialize(factor_ == 8);
        // In this case, we should be reading scalars and broadcasting them.
        output_.specialize(factor_ % vector_size == 0);
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::DepthwiseConv, DepthwiseConv)
HALIDE_REGISTER_GENERATOR(hannk::UpsampleChannels, UpsampleChannels)
