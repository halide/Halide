#include "Halide.h"

namespace {

constexpr int kSize = 64;

class Autograd : public Halide::Generator<Autograd> {
public:
    Input<Buffer<float>> input_a{"input_a", 1};
    Input<Buffer<float>> input_b{"input_b", 1};
    Input<Buffer<float>> input_c{"input_c", 1};

    // Test a case for which won't be able to find a derivative
    Input<Buffer<uint8_t>> lut{"lut", 1};
    Input<Buffer<uint8_t>> lut_indices{"lut_indices", 1};

    Output<Buffer<float>> output{"output", 1};
    Output<Buffer<uint8_t>> output_lut{"output_lut", 1};

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
