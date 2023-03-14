#include "Halide.h"
#include <iostream>

using namespace Halide;

int main(int argc, char **argv) {
    // Generate random input image.
    const int W = 128, H = 48;
    Buffer<uint8_t> in(W, H);
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
    max_x(x, y) = max(input(x - 1, y), input(x, y), input(x + 1, y));
    dilate3x3(x, y) = max(max_x(x, y - 1), max_x(x, y), max_x(x, y + 1));

    // Schedule.
    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        Var xi("xi"), yi("yi");
        dilate3x3.gpu_tile(x, y, xi, yi, 16, 16);
    } else if (target.has_feature(Target::HVX)) {
        dilate3x3.hexagon().vectorize(x, 64);
    } else {
        dilate3x3.vectorize(x, target.natural_vector_size<uint8_t>());
    }

    // Run the pipeline and verify the results are correct.
    Buffer<uint8_t> out = dilate3x3.realize({W, H}, target);

    for (int y = 1; y < H - 1; y++) {
        for (int x = 1; x < W - 1; x++) {
            uint16_t correct = std::max({std::max({in(x - 1, y - 1), in(x, y - 1), in(x + 1, y - 1)}),
                                         std::max({in(x - 1, y), in(x, y), in(x + 1, y)}),
                                         std::max({in(x - 1, y + 1), in(x, y + 1), in(x + 1, y + 1)})});

            if (out(x, y) != correct) {
                std::cout << "out(" << x << ", " << y << ") = " << out(x, y) << " instead of " << correct << "\n";
                return 1;
            }
        }
    }

    std::cout << "Success!\n";
    return 0;
}
