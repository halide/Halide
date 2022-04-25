#include "Halide.h"

namespace {

constexpr int kSize = 64;

class Autograd : public Halide::Generator<Autograd> {
public:
    Input<Buffer<float, 1>> input_a{"input_a"};
    Input<Buffer<float, 1>> input_b{"input_b"};
    Input<Buffer<float, 1>> input_c{"input_c"};

    // Test a case for which won't be able to find a derivative
    Input<Buffer<uint8_t, 1>> lut{"lut"};
    Input<Buffer<uint8_t, 1>> lut_indices{"lut_indices"};

    Output<Buffer<float, 1>> output{"output"};
    Output<Buffer<uint8_t, 1>> output_lut{"output_lut"};

    void generate() {
        lut.dim(0).set_bounds(0, 256);

        Var x;
        output(x) = 33 * pow(input_a(x), 3) +
                    22 * pow(input_b(x), 2) +
                    11 * input_c(x) +
                    1;

        output_lut(x) = lut(lut_indices(x));

        input_a.set_estimates({{0, kSize}});
        input_b.set_estimates({{0, kSize}});
        input_c.set_estimates({{0, kSize}});
        output.set_estimates({{0, kSize}});

        lut.set_estimates({{0, 256}});
        lut_indices.set_estimates({{0, kSize}});
        output_lut.set_estimates({{0, kSize}});

        output.vectorize(x, natural_vector_size<float>());
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Autograd, autograd)
