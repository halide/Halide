#include "Halide.h"
#include "halide/common_halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace hannk {

class DepthwiseConv : public Generator<DepthwiseConv> {
public:
    // If positive, a constant inverse depth multiplier.
    GeneratorParam<int> inv_depth_multiplier_{"inv_depth_multiplier", -1};

    // Unsigned 8-bit input tensor, indexed by ci, x, y, b.
    Input<Buffer<uint8_t>> input_{"input", 4};
    Input<uint8_t> input_zero_{"input_zero"};

    // A 3D array of 8-bit filter coefficients indexed by co, x, y.
    Input<Buffer<uint8_t>> filter_{"filter", 3};
    Input<uint8_t> filter_zero_{"filter_zero"};

    // A 1D array of 32-bit biases indexed by co.
    Input<Buffer<int32_t>> bias_{"bias", 1};

    // The depth multiplier specifies the ratio between co and ci.
    Input<int> depth_multiplier_{"depth_multiplier"};

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

    Input<int32_t> output_multiplier_{"output_multiplier"};
    Input<uint32_t> output_shift_{"output_shift"};
    Input<uint8_t> output_zero_{"output_zero"};
    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Output<Buffer<uint8_t>> output_{"output", 4};

    void generate() {
        // The algorithm.

        // Some free variables, where x and y represent the spatial dimensions.
        Var x("x"), y("y"), c("c"), b("b");

        // Apply the c multiplier.
        Func resampled_input("resampled_input");
        Expr c_resampled = inv_depth_multiplier_ >= 0 ? c * inv_depth_multiplier_ : c / depth_multiplier_;
        resampled_input(c, x, y, b) = input_(c_resampled, x, y, b);

        Func filter_zeroed("filter_zeroed");
        filter_zeroed(c, x, y) = i16(filter_(c, x, y)) - i16(filter_zero_);

        // Do the convolution in 32-bit.
        filter_.dim(1).set_min(0);
        filter_.dim(2).set_min(0);
        Expr filter_width = filter_.dim(1).extent();
        Expr filter_height = filter_.dim(2).extent();
        RDom r(0, filter_width, 0, filter_height);
        Expr filter_zeroed_rdxy = filter_zeroed(c, r.x, r.y);

        // We want to compute the reduction:
        // convolved(c, x, y, b) = bias_(c)
        // convolved(c, x, y, b) +=
        //    i32(filter_zeroed_rdxy) *
        //    (i32(input_rdxy) - i32(input_zero_))
        //
        // However, this requires subtracting the input zero at every output.
        // We can factor the reduction like so:
        //
        // convolved(c, x, y, b) = bias_(c)
        // convolved(c, x, y, b) +=
        //    i32(filter_zeroed_rdxy) * i32(input_rdxyc) -
        //    i32(filter_zeroed_rdxy) * i32(input_zero_)
        //
        // The latter reduction can be computed once per output channel.
        Func sum_filter("sum_filter");
        sum_filter(c) += i32(filter_zeroed_rdxy);

        Func offset_c("offset_c");
        offset_c(c) = bias_(c) - sum_filter(c) * i32(input_zero_);

        Expr input_rdxy =
            resampled_input(c, x * stride_x_ + r.x * dilation_x_, y * stride_y_ + r.y * dilation_y_, b);
        Func convolved("convolved");
        convolved(c, x, y, b) = offset_c(c);
        convolved(c, x, y, b) += i32(filter_zeroed_rdxy) * i32(input_rdxy);

        // Saturate and narrow the output.
        Expr output = multiply_2x_high(convolved(c, x, y, b), output_multiplier_);
        output = i16_sat(rounding_shift_right(output, output_shift_));
        output = u8_sat(saturating_add(output, output_zero_));
        output_(c, x, y, b) = clamp(output, output_min_, output_max_);

        // Schedule.
        interpret_as_tensor(input_);
        interpret_as_tensor(filter_);
        interpret_as_tensor(bias_);
        interpret_as_tensor(output_);
        require_same_min_extent(3, input_, output_);
        require_same_min_extent(0, bias_, output_);
        require_same_min_extent(0, filter_, output_);

        if (inv_depth_multiplier_ == 0) {
            // When we're broadcasting input channels, require that the input has only
            // one channel.
            input_.dim(0).set_extent(1);
        }

        int vector_size = natural_vector_size<uint8_t>();
        if (get_register_count(target) < 32) {
            vector_size = natural_vector_size<int16_t>();
        }

        // Tile the output, so we can try to re-use loads spatially when performing
        // convolution. This also helps because we can schedule the input and not
        // waste work for strides less than the tile size.
        // We split co and reorder it outermost, so we can maximize locality of the
        // filter. We even put it outside of the batch loop, so we can compute the
        // boundary condition on the filter at co and reuse it across batches.
        const int kTileW = 2;
        const int kTileH = 2;
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

        if (inv_depth_multiplier_ < 0) {
            // The reason inv_depth_multiplier_ is a GeneratorParam and not a
            // specialization is that we can't specialize the (lack of) compute_at here.
            resampled_input
                .compute_at(output_, b)
                .store_in(MemoryType::Stack)
                .vectorize(c, vector_size, TailStrategy::GuardWithIf);

            resampled_input.specialize(depth_multiplier_ == 1);
        }

        // This doesn't read from any of the inputs directly, so we can vectorize
        // rounding up.
        offset_c.compute_at(output_, co)
            .store_in(MemoryType::Stack)
            .vectorize(c, vector_size, TailStrategy::RoundUp);

        filter_zeroed.compute_at(output_, co)
            .store_in(MemoryType::Stack)
            .align_storage(c, vector_size)
            .vectorize(c, vector_size, TailStrategy::PredicateLoads);

        bias_.in().compute_at(output_, co)
            .store_in(MemoryType::Stack)
            .vectorize(_0, vector_size, TailStrategy::PredicateLoads);
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::DepthwiseConv, DepthwiseConv)
