#include "Halide.h"
#include "halide/common_halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;
using namespace Halide::ConciseCasts;

namespace hannk {

class AveragePool : public Generator<AveragePool> {
public:
    // Unsigned 8-bit input tensor, indexed by c, x, y, b.
    Input<Buffer<uint8_t>> input_{"input", 4};

    // The stride specifies how the input [x, y] are sub-subsampled. For every
    // spatial location [x, y] in the output buffer, the input buffer is sampled
    // spatially at [x * stride, y * stride].
    Input<int> stride_x_{"stride_x"};
    Input<int> stride_y_{"stride_y"};
    Input<int> filter_width_{"filter_width"};
    Input<int> filter_height_{"filter_height"};

    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Output<Buffer<uint8_t>> output_{"output", 4};

    void generate() {
        // The algorithm.
        Var c("c"), x("x"), y("y"), b("b");

        Expr min_x = input_.dim(1).min();
        Expr max_x = input_.dim(1).max();
        Expr min_y = input_.dim(2).min();
        Expr max_y = input_.dim(2).max();

        // This pipeline conceptually requires a zero padding boundary condition.
        // However, zero padding is messy. To avoid this, we'll just use a clamp to
        // avoid out of bounds reads, and then use 'where' on the RDom to avoid
        // including these values in the reduction.
        Func input_bounded("input_bounded");
        input_bounded(c, x, y, b) =
            input_(c, clamp(x, min_x, max_x), clamp(y, min_y, max_y), b);

        RDom r(0, filter_width_, 0, filter_height_);
        Expr x_rx = x * stride_x_ + r.x;
        Expr y_ry = y * stride_y_ + r.y;
        r.where(min_x <= x_rx && x_rx <= max_x && min_y <= y_ry && y_ry <= max_y);

        Func sum("sum");
        sum(c, x, y, b) += u16(input_bounded(c, x_rx, y_ry, b));

        Func average("average");
        // TODO: We should probably specialize/optimize for the case
        // where filter_count = filter_width * filter_height.
        Expr x_start = max(x * stride_x_, min_x);
        Expr x_end = min(x * stride_x_ + filter_width_, max_x + 1);
        Expr y_start = max(y * stride_y_, min_y);
        Expr y_end = min(y * stride_y_ + filter_height_, max_y + 1);
        Expr filter_count = (x_end - x_start) * (y_end - y_start);
        average(c, x, y, b) = u8_sat((sum(c, x, y, b) + filter_count / 2) / filter_count);

        output_(c, x, y, b) = clamp(average(c, x, y, b), output_min_, output_max_);

        // Schedule.
        require_same_min_extent(0, input_, output_);
        require_same_min_extent(3, input_, output_);

        // TODO: Optimize more.
        const int vector_size = natural_vector_size<uint8_t>();
        output_.compute_root()
            .vectorize(c, vector_size, TailStrategy::Predicate);
    }
};

class MaxPool : public Generator<MaxPool> {
public:
    // Unsigned 8-bit input tensor, indexed by c, x, y, b.
    Input<Buffer<uint8_t>> input_{"input", 4};

    // The stride specifies how the input [x, y] are sub-subsampled. For every
    // spatial location [x, y] in the output buffer, the input buffer is sampled
    // spatially at [x * stride, y * stride].
    Input<int> stride_x_{"stride_x"};
    Input<int> stride_y_{"stride_y"};
    Input<int> filter_width_{"filter_width"};
    Input<int> filter_height_{"filter_height"};

    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Output<Buffer<uint8_t>> output_{"output", 4};

    void generate() {
        // The algorithm.
        Var c("c"), x("x"), y("y"), b("b");

        Expr min_x = input_.dim(1).min();
        Expr max_x = input_.dim(1).max();
        Expr min_y = input_.dim(2).min();
        Expr max_y = input_.dim(2).max();

        Func input_bounded("input_bounded");
        input_bounded(c, x, y, b) =
            input_(c, clamp(x, min_x, max_x), clamp(y, min_y, max_y), b);

        Func maximum("maximum");
        RDom r(0, filter_width_, 0, filter_height_);
        Expr x_rx = x * stride_x_ + r.x;
        Expr y_ry = y * stride_y_ + r.y;
        // Unlike pools that sum the input, we can use a clamp boundary condition
        // here. However, it still seems faster to include this where clause.
        r.where(min_x <= x_rx && x_rx <= max_x && min_y <= y_ry && y_ry <= max_y);
        maximum(c, x, y, b) = output_min_;
        maximum(c, x, y, b) = max(maximum(c, x, y, b), input_bounded(c, x_rx, y_ry, b));

        output_(c, x, y, b) = min(maximum(c, x, y, b), output_max_);

        // Schedule.

        // TODO: Optimize more.
        output_.compute_root();

        const int vector_size = natural_vector_size<uint8_t>();
        Expr output_channels = output_.dim(0).extent();
        for (int i : {4, 2, 1}) {
            output_.specialize(output_channels >= vector_size * i)
                .vectorize(c, vector_size * i, TailStrategy::ShiftInwards);
        }
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::AveragePool, AveragePool)
HALIDE_REGISTER_GENERATOR(hannk::MaxPool, MaxPool)
