#include "Halide.h"
#include "common_halide.h"

using namespace Halide;

namespace hannk {

class Fill : public Generator<Fill> {
public:
    Input<uint8_t> pad_value_{"pad_value"};

    Output<Buffer<uint8_t>> output_{"output", 4};

    void generate() {
        Var c("c"), x("x"), y("y"), b("b");

        output_(c, x, y, b) = pad_value_;

        // Schedule.
        const int vector_size_u8 = natural_vector_size<uint8_t>();

        Expr output_channels = output_.dim(0).extent();

        output_.specialize(is_interleaved(output_, 4))
            .vectorize(x, vector_size_u8, TailStrategy::GuardWithIf)
            .unroll(c);

        for (int i = vector_size_u8; i >= 4; i /= 2) {
            output_
                .specialize(output_channels >= i)
                .vectorize(c, i, TailStrategy::ShiftInwards);
        }

        output_
            .vectorize(c, vector_size_u8, TailStrategy::GuardWithIf);
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::Fill, Fill)
