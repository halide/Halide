#include "Halide.h"

namespace {

class Autograd : public Halide::Generator<Autograd> {
public:
    Input<Buffer<float>> input_a{ "input_a", 1 };
    Input<Buffer<float>> input_b{ "input_b", 1 };
    Input<Buffer<float>> input_c{ "input_c", 1 };

    Output<Buffer<float>> output{ "output", 1 };

    void generate() {
        Var x;
        output(x) = 33 * pow(input_a(x), 3) +
                    22 * pow(input_b(x), 2) +
                    11 * input_c(x) +
                    1;

        input_a.set_estimates({{0, 32}});
        input_b.set_estimates({{0, 32}});
        input_c.set_estimates({{0, 32}});
        output.set_estimates({{0, 32}});

        output.vectorize(x, natural_vector_size<float>());
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Autograd, autograd)
