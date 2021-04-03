#include "Halide.h"
#include "common_halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;
using namespace Halide::ConciseCasts;

namespace hannk {

class MaxPool : public Generator<MaxPool> {
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
        input_bounded(c, x, y, b) =
            constant_exterior(input_, output_min_)(c, x, y, b);

        Func maximum("maximum");
        RDom r(0, filter_width_, 0, filter_height_);
        maximum(c, x, y, b) = output_min_;
        maximum(c, x, y, b) =
            max(maximum(c, x, y, b),
                input_bounded(c, x * stride_x_ + r.x, y * stride_y_ + r.y, b));

        output_(c, x, y, b) = min(maximum(c, x, y, b), output_max_);

        // Schedule.
        require_same_min_extent(0, input_, output_);
        require_same_min_extent(3, input_, output_);

        // TODO: Optimize more.
        const int vector_size = natural_vector_size<uint8_t>();
        output_.compute_root()
            .vectorize(c, vector_size, TailStrategy::Predicate);
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::MaxPool, MaxPool)
