#include "Halide.h"
#include "common_halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;

namespace hannk {

class Copy : public Generator<Copy> {
public:
    Input<Buffer<uint8_t>> input_{"input", 4};
    Input<uint8_t> pad_value_{"pad_value"};

    Output<Buffer<uint8_t>> output_{"output", 4};

    void generate() {
        Var c("c"), x("x"), y("y"), b("b");

        output_(c, x, y, b) = constant_exterior(input_, pad_value_)(c, x, y, b);

        // Schedule.
        const int vector_size_u8 = natural_vector_size<uint8_t>();

        Expr input_channels = input_.dim(0).extent();
        Expr output_channels = output_.dim(0).extent();

        // Handle 3 channel -> 4 channel padding as a special case.
        // TODO: vectorize c instead of unroll c.
        output_.specialize(is_interleaved(input_, 3) && is_interleaved(output_, 4))
            .vectorize(x, vector_size_u8, TailStrategy::GuardWithIf)
            .unroll(c);

        // Handle cases with a small number of channels.
        for (int i = vector_size_u8; i >= 4; i /= 2) {
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
            .vectorize(c, vector_size_u8, TailStrategy::GuardWithIf)
            .specialize(input_channels == output_channels);

        // Don't support padding on the batch dimension. This reduces
        // the amount of code generated.
        // TODO: Maybe we should only handle 2 or 3 dimensions in Halide
        // to further reduce code size.
        require_same_min_extent(3, input_, output_);
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::Copy, Copy)
