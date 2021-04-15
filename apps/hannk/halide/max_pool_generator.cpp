#include "Halide.h"
#include "common_halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace hannk {

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

        Func maximum("maximum");
        RDom r(0, filter_width_, 0, filter_height_);
        maximum(c, x, y, b) = output_min_;
        maximum(c, x, y, b) =
            max(maximum(c, x, y, b), input_(c, x * stride_x_ + r.x, y * stride_y_ + r.y, b));

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

HALIDE_REGISTER_GENERATOR(hannk::MaxPool, MaxPool)
