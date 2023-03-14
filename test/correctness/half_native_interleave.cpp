#include "Halide.h"
#include <iostream>

using namespace Halide;

int main(int argc, char **argv) {
    // Generate random input.
    const int W = 256;
    Buffer<uint8_t> input(W);
    for (int x = 0; x < W; x++) {
        input(x) = rand() & 0xff;
    }

    Var x("x");
    Func input_16("input_16"), product("product"), sum("sum"), diff("difference");
    input_16(x) = cast<int16_t>(input(x));

    product(x) = (input_16(x) * 2);
    sum(x) = (input_16(x) + 2);
    diff(x) = (input_16(x) - 2);

    // Schedule.
    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::HVX)) {
        // Vectorize by one vector width.
        // Since the operations are widening ops,
        // the operands are effectively half-vector width.
        // The assertion referenced in issue below
        // shouldn't be triggered:
        // https://github.com/halide/Halide/issues/1582
        product.hexagon().vectorize(x, 64);
        sum.hexagon().vectorize(x, 64);
        diff.hexagon().vectorize(x, 64);
    } else {
        product.vectorize(x, target.natural_vector_size<uint8_t>());
        sum.vectorize(x, target.natural_vector_size<uint8_t>());
        diff.vectorize(x, target.natural_vector_size<uint8_t>());
    }

    // Run the pipeline and verify the results are correct.
    Buffer<int16_t> out_p = product.realize({W}, target);
    Buffer<int16_t> out_s = sum.realize({W}, target);
    Buffer<int16_t> out_d = diff.realize({W}, target);

    for (int x = 1; x < W - 1; x++) {
        int16_t correct_p = input(x) * 2;
        int16_t correct_s = input(x) + 2;
        int16_t correct_d = input(x) - 2;

        if (out_p(x) != correct_p) {
            std::cout << "out_p(" << x << ") = " << out_p(x) << " instead of " << correct_p << "\n";
            return 1;
        }
        if (out_s(x) != correct_s) {
            std::cout << "out_s(" << x << ") = " << out_s(x) << " instead of " << correct_s << "\n";
            return 1;
        }
        if (out_d(x) != correct_d) {
            std::cout << "out_d(" << x << ") = " << out_d(x) << " instead of " << correct_d << "\n";
            return 1;
        }
    }

    std::cout << "Success!\n";
    return 0;
}
