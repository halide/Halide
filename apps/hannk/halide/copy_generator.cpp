#include "Halide.h"
#include "halide/common_halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;

namespace hannk {

// TODO: It might be better to implement this in C++ and not Halide. It's a trivial pipeline.
class Copy : public Generator<Copy> {
public:
    Input<Buffer<void, 4>> input_{"input"};
    Input<int> pad_value_{"pad_value"};
    Output<Buffer<void, 4>> output_{"output"};

    void generate() {
        Var c("c"), x("x"), y("y"), b("b");

        // This pipeline only supports padding dimension 0.
        Expr pad_value = cast(input_.type(), pad_value_);
        Func input_bounded =
            constant_exterior(input_, pad_value, {{input_.dim(0).min(), input_.dim(0).extent()}});

        output_(c, x, y, b) = cast(output_.type(), input_bounded(c, x, y, b));

        // Schedule.
        const int vector_size =
            std::max(natural_vector_size(output_.type()), natural_vector_size(input_.type()));

        if (input_.type() == UInt(8) && output_.type() == UInt(8)) {
            // Handle 3 channel -> 4 channel padding as a special case.
            // TODO: vectorize c instead of unroll c.
            output_.specialize(is_interleaved(input_, 3) && is_interleaved(output_, 4))
                .vectorize(x, vector_size, TailStrategy::GuardWithIf)
                .unroll(c);
        }

        Expr input_channels = input_.dim(0).extent();
        Expr output_channels = output_.dim(0).extent();
        Expr channels_in_bounds =
            input_.dim(0).min() == output_.dim(0).min() && input_channels == output_channels;

        // Handle cases with a small number of channels.
        for (int i = vector_size; i >= 2; i /= 2) {
            output_.specialize(output_channels >= i)
                .vectorize(c, i, TailStrategy::ShiftInwards)
                .reorder(c, x, y, b)
                .specialize(channels_in_bounds);
        }

        // In the general case, use GuardWithIf and reorder c
        // away from the inner loop to reduce the if overhead.
        output_
            .reorder(x, y, c, b)
            .vectorize(c, vector_size, TailStrategy::GuardWithIf)
            .specialize(channels_in_bounds);
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::Copy, Copy)
