#include "Halide.h"
#include "halide/common_halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace hannk {

class Mean : public Generator<Mean> {
public:
    Input<Buffer<uint8_t, 4>> input_{"input"};

    // The bounds of the region to reduce. This pipeline is
    // implemented as a stencil over this reach at each output.
    // However, the expected usage is to have either the output
    // extent be 1, or the reduction extent be 1.
    Input<int> c_min_{"c_min"};
    Input<int> c_extent_{"c_extent"};
    Input<int> x_min_{"x_min"};
    Input<int> x_extent_{"x_extent"};
    Input<int> y_min_{"y_min"};
    Input<int> y_extent_{"y_extent"};
    Input<int> b_min_{"b_min"};
    Input<int> b_extent_{"b_extent"};

    Output<Buffer<uint8_t, 4>> output_{"output"};

    void generate() {
        // The algorithm.
        Var c("c"), x("x"), y("y"), b("b");

        Func sum("sum");
        RDom r(c_min_, c_extent_, x_min_, x_extent_, y_min_, y_extent_, b_min_, b_extent_);
        sum(c, x, y, b) += u32(input_(c + r.x, x + r.y, y + r.z, b + r.w));

        Expr extent = c_extent_ * x_extent_ * y_extent_ * b_extent_;
        output_(c, x, y, b) = u8((sum(c, x, y, b) + extent / 2) / extent);

        // Schedule.
        output_.compute_root()
            .vectorize(c, natural_vector_size<uint8_t>(), TailStrategy::GuardWithIf);

        // If we aren't reducing c, just moving it out of the inner loop and
        // vectorizing c is pretty good.
        sum.compute_at(output_, x)
            .update()
            .reorder(r.y, r.z, r.x, r.w)
            .vectorize(c, natural_vector_size<uint8_t>(), TailStrategy::GuardWithIf);

        // TODO: If we want to reduce c, we should horizontally vectorize r.x.
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::Mean, Mean)
