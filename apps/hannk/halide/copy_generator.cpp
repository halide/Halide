#include "Halide.h"
#include "common_halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;

namespace hannk {

// TODO: It might be better to implement this in C++ and not Halide. It's a trivial pipeline.
class Copy : public Generator<Copy> {
public:
    Input<Buffer<>> input_{"input", 4};

    Output<Buffer<>> output_{"output", 4};

    void generate() {
        Var c("c"), x("x"), y("y"), b("b");

        output_(c, x, y, b) = cast(output_.type(), input_(c, x, y, b));

        // Schedule.
        const int vector_size =
            std::max(natural_vector_size(output_.type()), natural_vector_size(input_.type()));

        Expr input_channels = input_.dim(0).extent();
        Expr output_channels = output_.dim(0).extent();

        if (input_.type() == UInt(8) && output_.type() == UInt(8)) {
            // Handle 3 channel -> 4 channel padding as a special case.
            // TODO: vectorize c instead of unroll c.
            output_.specialize(is_interleaved(input_, 3) && is_interleaved(output_, 4))
                .vectorize(x, vector_size, TailStrategy::GuardWithIf)
                .unroll(c);
        }

        // Handle cases with a small number of channels.
        for (int i = vector_size; i >= 2; i /= 2) {
            output_.specialize(output_channels >= i)
                .vectorize(c, i, TailStrategy::ShiftInwards)
                .reorder(x, y, c, b)
                .specialize(input_channels == output_channels)
                // TODO: This is ridiculous but it helps.
                .specialize(output_channels < 100)
                .reorder(c, x, y, b);
        }

        // In the general case, use GuardWithIf and reorder c
        // away from the inner loop to reduce the if overhead.
        output_
            .reorder(x, y, c, b)
            .vectorize(c, vector_size, TailStrategy::GuardWithIf)
            .specialize(input_channels == output_channels);
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::Copy, Copy)
