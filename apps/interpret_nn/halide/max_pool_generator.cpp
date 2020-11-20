#include "Halide.h"
#include "common_halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;
using namespace Halide::ConciseCasts;

namespace interpret_nn {

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

        Func input_bounded("input_bounded");
        input_bounded(c, x, y, b) = constant_exterior(input_, 0)(c, x, y, b);

        Func maximum("maximum");
        RDom filter_dom(0, filter_width_, 0, filter_height_);
        maximum(c, x, y, b) = output_min_;
        maximum(c, x, y, b) =
            max(maximum(c, x, y, b),
                input_bounded(c, x * stride_x_ + filter_dom.x, y * stride_y_ + filter_dom.y, b));

        output_(c, x, y, b) = min(maximum(c, x, y, b), output_max_);

        // Schedule.
        interpret_as_tensor(input_);
        interpret_as_tensor(output_);

        // TODO: Optimize more.
        Expr output_channels = output_.dim(0).extent();
        const int vector_size = natural_vector_size<uint8_t>();
        output_.compute_root()
            .specialize(output_channels >= vector_size)
            .vectorize(c, vector_size, TailStrategy::ShiftInwards);
    }
};

}  // namespace interpret_nn

HALIDE_REGISTER_GENERATOR(interpret_nn::MaxPool, MaxPool)
