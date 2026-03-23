#include "Halide.h"
#include <iostream>

using namespace Halide;

// Given a 3x3 patch, find the middle element
// We do this by first finding the minimum, maximum, and middle for each row.
// Then across rows, we find the maximum minimum, the minimum maximum, and the middle middle.
// Then we take the middle of those three results.

Expr mid3(Expr a, Expr b, Expr c) {
    return max(min(max(a, b), c), min(a, b));
}

int main(int arch, char **argv) {
    const int W = 256, H = 256;
    Buffer<uint8_t> in(W, H);
    // Set up the input.
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            in(x, y) = rand() & 0xff;
        }
    }

    Var x("x"), y("y");

    // Boundary condition.
    Func input = BoundaryConditions::constant_exterior(in, 0);
    input.compute_root();

    // Algorithm.
    Func max_x("max_x"), min_x("min_x"), mid_x("mid_x");
    max_x(x, y) = max(input(x - 1, y), input(x, y), input(x + 1, y));
    min_x(x, y) = min(input(x - 1, y), input(x, y), input(x + 1, y));
    mid_x(x, y) = mid3(input(x - 1, y), input(x, y), input(x + 1, y));

    Func min_max("min_max"), max_min("max_min"), mid_mid("mid_mid");
    min_max(x, y) = min(max_x(x, y - 1), max_x(x, y), max_x(x, y + 1));
    max_min(x, y) = max(min_x(x, y - 1), min_x(x, y), min_x(x, y + 1));
    mid_mid(x, y) = mid3(mid_x(x, y - 1), mid_x(x, y), mid_x(x, y + 1));

    Func median3x3("median3x3");
    median3x3(x, y) = mid3(min_max(x, y), max_min(x, y), mid_mid(x, y));

    // Schedule.
    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        Var xi("xi"), yi("yi");
        median3x3.gpu_tile(x, y, xi, yi, 16, 16);
    } else if (target.has_feature(Target::HVX)) {
        median3x3.hexagon().vectorize(x, 64);
    } else {
        median3x3.vectorize(x, target.natural_vector_size<uint8_t>());
    }

    // Run the pipeline and verify the results are correct.
    Buffer<uint8_t> out = median3x3.realize({W, H}, target);

    for (int y = 1; y < H - 1; y++) {
        for (int x = 1; x < W - 1; x++) {
            uint8_t inp[9] = {in(x - 1, y - 1), in(x, y - 1), in(x + 1, y - 1),
                              in(x - 1, y), in(x, y), in(x + 1, y),
                              in(x - 1, y + 1), in(x, y + 1), in(x + 1, y + 1)};
            std::nth_element(&inp[0], &inp[4], &inp[9]);
            uint8_t correct = inp[4];
            if (correct != out(x, y)) {
                std::cout << "out(" << x << ", " << y << ") = " << out(x, y) << " instead of " << correct << "\n";
                return 1;
            }
        }
    }
    std::cout << "Success!\n";
    return 0;
}
