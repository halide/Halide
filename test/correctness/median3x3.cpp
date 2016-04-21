#include "Halide.h"
#include <iostream>

using namespace Halide;

// Given a 3x3 patch, find the middle element
// We do this by first finding the minimum, maximum, and middle for each row.
// Then across rows, we find the maximum minimum, the minimum maximum, and the middle middle.
// Then we take the middle of those three results.


Expr max3(Expr a, Expr b, Expr c) {
    return max(max(a, b), c);
}
Expr min3(Expr a, Expr b, Expr c) {
    return min(min(a, b), c);
}
Expr mid3(Expr a, Expr b, Expr c) {
    return max(min(max(a, b), c), min(a, b));
}
uint8_t max3(uint8_t a, uint8_t b, uint8_t c) {
    return std::max(std::max(a, b), c);
}
uint8_t min3(uint8_t a, uint8_t b, uint8_t c) {
    return std::min(std::min(a, b), c);
}
uint8_t mid3(uint8_t a, uint8_t b, uint8_t c) {
    return std::max(std::min(std::max(a, b), c), std::min(a, b));
}

int main(int arch, char **argv) {
    const int W = 256, H = 256;
    Image<uint8_t> in(W, H);
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
    max_x(x, y) = max3(input(x-1, y), input(x, y), input(x+1, y));
    min_x(x, y) = min3(input(x-1, y), input(x, y), input(x+1, y));
    mid_x(x, y) = mid3(input(x-1, y), input(x, y), input(x+1, y));

    Func min_max("min_max"), max_min("max_min"), mid_mid("mid_mid");
    min_max(x, y) = min3(max_x(x, y-1), max_x(x, y), max_x(x, y+1));
    max_min(x, y) = max3(min_x(x, y-1), min_x(x, y), min_x(x, y+1));
    mid_mid(x, y) = mid3(mid_x(x, y-1), mid_x(x, y), mid_x(x, y+1));

    Func median3x3("median3x3");
    median3x3(x, y) = mid3(min_max(x, y), max_min(x, y), mid_mid(x, y));

    // Schedule.
    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        median3x3.gpu_tile(x, y, 16, 16);
    } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        median3x3.hexagon().vectorize(x, 64);
    } else {
        median3x3.vectorize(x, target.natural_vector_size<uint8_t>());
    }

    // Run the pipeline and verify the results are correct.
    Image<uint8_t> out = median3x3.realize(W, H, target);

    for (int y = 1; y < H-1; y++) {
        for (int x = 1; x < W-1; x++) {
            uint8_t max_y = max3(in(x-1, y), in(x, y), in(x+1, y));
            uint8_t min_y = min3(in(x-1, y), in(x, y), in(x+1, y));
            uint8_t mid_y = mid3(in(x-1, y), in(x, y), in(x+1, y));

            uint8_t max_u = max3(in(x-1, y-1), in(x, y-1), in(x+1, y-1));
            uint8_t min_u = min3(in(x-1, y-1), in(x, y-1), in(x+1, y-1));
            uint8_t mid_u = mid3(in(x-1, y-1), in(x, y-1), in(x+1, y-1));

            uint8_t max_d = max3(in(x-1, y+1), in(x, y+1), in(x+1, y+1));
            uint8_t min_d = min3(in(x-1, y+1), in(x, y+1), in(x+1, y+1));
            uint8_t mid_d = mid3(in(x-1, y+1), in(x, y+1), in(x+1, y+1));

            uint8_t min_max = min3(max_u, max_y, max_d);
            uint8_t max_min = max3(min_u, min_y, min_d);
            uint8_t mid_mid = mid3(mid_u, mid_y, mid_d);

            uint8_t correct = mid3(min_max, max_min, mid_mid);
            if (correct != out(x, y)) {
                std::cout << "out(" << x << ", " << y << ") = " << out(x, y) << " instead of " << correct << "\n";
                return -1;
            }
        }
    }
    std::cout << "Success!\n";
    return 0;
}
