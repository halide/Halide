#include "Halide.h"

namespace {

class Example : public Halide::Generator<Example> {
public:
    Input<Buffer<uint8_t, 3>> image_{"image"};
    Output<Buffer<uint8_t, 3>> output_{"output"};

    void generate() {
        using Halide::BoundaryConditions::repeat_edge;

        Var x("x"), y("y"), c("c");

        Func input_clamped("input_clamped");
        Func alpha_values("alpha_values");
        Func output0("output0"), output1("output1");

        input_clamped(x, y, c) = repeat_edge(image_)(x, y, c);
        alpha_values(x, y) = ((x + y) % 256) / 255.f;

        const auto upsample = [&](Func f) {
            Func up_x, up_y;
            up_x(x, y, c) = (f((x / 2) - 1 + 2 * (x % 2), y, c));
            up_y(x, y, c) = (up_x(x, (y / 2) - 1 + 2 * (y % 2), c) + up_x(x, y / 2, c));

            return up_y;
        };

        Expr alpha = alpha_values(x, y);
        Expr input = input_clamped(x, y, c);
        output1(x, y, c) = lerp(input, upsample(input_clamped)(x, y, c), alpha);
        output0(x, y, c) = lerp(input, upsample(output1)(x, y, c), alpha);

        alpha_values.compute_root().vectorize(x, 8);
        input_clamped.compute_root().vectorize(x, 8);
        output1.compute_root().vectorize(x, 8);
        output0.compute_root().vectorize(x, 8);

        output_ = output0;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Example, example)
