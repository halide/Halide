#include "Halide.h"
#include "common_halide.h"

namespace interpret_nn {

using Halide::Generator;
using Halide::BoundaryConditions::constant_exterior;
using Halide::ConciseCasts::u16;
using Halide::ConciseCasts::u8_sat;

class AveragePool : public Generator<AveragePool> {
public:
    // Unsigned 8-bit input tensor, indexed by c, x, y, b.
    Input<Buffer<uint8_t>> input_{"input", 4};

    // The stride specifies how the input [x, y] are sub-subsampled. For every
    // spatial location [x, y] in the output buffer, the input buffer is sampled
    // spatially at [x * stride, y * stride].
    Input<int> stride_x_{"stride_x", 1, 1, 16};
    Input<int> stride_y_{"stride_y", 1, 1, 16};
    Input<int> filter_width_{"filter_width", 1, 1, 16};
    Input<int> filter_height_{"filter_height", 1, 1, 16};

    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Output<Buffer<uint8_t>> output_{"output", 4};

    void generate() {
        // The algorithm.
        Var c("c"), x("x"), y("y"), b("b");

        Func input_bounded("input_bounded");
        input_bounded(c, x, y, b) = constant_exterior(input_, 0)(c, x, y, b);

        Func sum("sum");
        RDom filter_dom(0, filter_width_, 0, filter_height_);
        sum(c, x, y, b) += u16(
            input_bounded(c, x * stride_x_ + filter_dom.x,
                          y * stride_y_ + filter_dom.y, b));

        Func average("average");
        Expr x_start = max(x * stride_x_, input_.dim(1).min());
        Expr x_end = min(x * stride_x_ + filter_width_, input_.dim(1).max() + 1);
        Expr y_start = max(y * stride_y_, input_.dim(2).min());
        Expr y_end = min(y * stride_y_ + filter_height_, input_.dim(2).max() + 1);
        Expr filter_count = (x_end - x_start) * (y_end - y_start);
        average(c, x, y, b) = u8_sat((sum(c, x, y, b) + filter_count / 2) / filter_count);

        output_(c, x, y, b) = clamp(average(c, x, y, b), output_min_, output_max_);

        // Schedule.
        InterpretAsTensor(input_);
        InterpretAsTensor(output_);

        // TODO: Optimize more.
        Expr output_channels = output_.dim(0).extent();
        const int vector_size = natural_vector_size<uint8_t>();
        output_.compute_root()
            .specialize(output_channels >= vector_size)
            .vectorize(c, vector_size, TailStrategy::ShiftInwards);
    }
};

}  // namespace interpret_nn

HALIDE_REGISTER_GENERATOR(interpret_nn::AveragePool, AveragePool)
