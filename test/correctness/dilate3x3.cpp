#include "Halide.h"
#include <iostream>

using namespace Halide;

Expr max3(Expr a, Expr b, Expr c) {
    return max(max(a, b), c);
}

uint8_t max3(uint8_t a, uint8_t b, uint8_t c) {
    return std::max(std::max(a, b), c);
}

int main(int argc, char **argv) {
    // Generate random input image.
    const int W = 128, H = 48;
    Image<uint8_t> in(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            in(x, y) = rand() & 0xff;
        }
    }

    Var x("x"), y("y");

    // Apply the boundary condition up-front.
    Func input = BoundaryConditions::repeat_edge(in);
    input.compute_root();

    // Define the dilate algorithm.
    Func max_x("max_x");
    Func dilate3x3("dilate3x3");
    max_x(x, y) = max3(input(x-1, y), input(x, y), input(x+1, y));
    dilate3x3(x, y) = max3(max_x(x, y-1), max_x(x, y), max_x(x, y+1));

    // Schedule.
    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        dilate3x3.gpu_tile(x, y, 16, 16);
    } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        dilate3x3.hexagon().vectorize(x, 64);
    } else {
        dilate3x3.vectorize(x, target.natural_vector_size<uint8_t>());
    }

    // Run the pipeline and verify the results are correct.
    Image<uint8_t> out = dilate3x3.realize(W, H, target);

    for (int y = 1; y < H-1; y++) {
        for (int x = 1; x < W-1; x++) {
            uint16_t correct = max3(max3(in(x-1, y-1), in(x, y-1), in(x+1, y-1)),
                                    max3(in(x-1, y  ), in(x, y  ), in(x+1, y  )),
                                    max3(in(x-1, y+1), in(x, y+1), in(x+1, y+1)));

            if (out(x, y) != correct) {
                std::cout << "out(" << x << ", " << y << ") = " << out(x, y) << " instead of " << correct << "\n";
                return -1;
            }
        }
    }

    std::cout << "Success!\n";
    return 0;
}
