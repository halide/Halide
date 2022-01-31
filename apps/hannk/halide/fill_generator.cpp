#include "Halide.h"
#include "halide/common_halide.h"

using namespace Halide;

namespace hannk {

// TODO: It might be better to implement this in C++ and not Halide. It's a trivial pipeline.
class Fill : public Generator<Fill> {
public:
    // Value to fill the output with.
    Input<uint8_t> value_{"value"};
    Output<Buffer<uint8_t, 4>> output_{"output"};

    void generate() {
        Var c("c"), x("x"), y("y"), b("b");

        output_(c, x, y, b) = value_;

        // Schedule.
        const int vector_size_u8 = natural_vector_size<uint8_t>();

        output_.specialize(is_interleaved(output_, 4))
            .vectorize(x, vector_size_u8, TailStrategy::GuardWithIf)
            .vectorize(c);

        Expr output_channels = output_.dim(0).extent();
        for (int i = vector_size_u8; i >= 4; i /= 2) {
            output_
                .specialize(output_channels >= i)
                .vectorize(c, i, TailStrategy::ShiftInwards);
        }

        output_.vectorize(c, vector_size_u8, TailStrategy::GuardWithIf);
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::Fill, Fill)
