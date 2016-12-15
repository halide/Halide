#include "Halide.h"
#include <iostream>

using namespace Halide;

int main(int arch, char **argv) {
    const int W = 256, H = 256;

    Buffer<uint8_t> in(W, H);
    // Set up the input.
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            in(x, y) = rand() & 0xff;
        }
    }

    // Define a convolution kernel, and its sum.
    Buffer<uint8_t> kernel(3, 3);
    kernel.set_min(-1, -1);
    int kernel_sum = 0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            kernel(x, y) = rand() % 0x7;
            kernel_sum += kernel(x, y);
        }
    }

    Var x("x"), y("y");
    RDom r(-1, 3, -1, 3);

    // Boundary condition.
    Func input = BoundaryConditions::repeat_edge(in);
    input.compute_root();

    // Test a widening reduction, followed by a narrowing.
    {
        Func f;
        f(x, y) = sum(cast<uint16_t>(input(x + r.x, y + r.y)) * kernel(r.x, r.y));

        Func g;
        g(x, y) = cast<uint8_t>(f(x, y) / kernel_sum);

        // Schedule.
        Target target = get_jit_target_from_environment();
        if (target.has_gpu_feature()) {
            g.gpu_tile(x, y, 16, 16);
        } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
            g.hexagon().vectorize(x, 128);
        } else {
            g.vectorize(x, target.natural_vector_size<uint8_t>());
        }

        // Run the pipeline and verify the results are correct.
        Buffer<uint8_t> out = g.realize(W, H, target);

        for (int y = 1; y < H-1; y++) {
            for (int x = 1; x < W-1; x++) {
                uint16_t correct = 0;
                for (int ry = -1; ry <= 1; ry++) {
                    for (int rx = -1; rx <= 1; rx++) {
                        correct += static_cast<uint16_t>(in(x + rx, y + ry)) * kernel(rx, ry);
                    }
                }
                correct /= kernel_sum;
                if (correct != out(x, y)) {
                    std::cout << "out(" << x << ", " << y << ") = " << (int)out(x, y) << " instead of " << correct << "\n";
                    return -1;
                }
            }
        }
    }

    // Test a tuple reduction with widening, followed by narrowing the result.
    {
        Func f;
        f(x, y) = { cast<uint16_t>(0), cast<uint8_t>(0) };
        f(x, y) = {
            f(x, y)[0] + cast<uint16_t>(input(x + r.x, y + r.y)) * kernel(r.x, r.y),
            f(x, y)[1] + kernel(r.x, r.y),
        };

        Func g;
        g(x, y) = cast<uint8_t>((f(x, y)[0] + f(x, y)[1]) / kernel_sum);

        // Schedule.
        Target target = get_jit_target_from_environment();
        if (target.has_gpu_feature()) {
            g.gpu_tile(x, y, 16, 16);
        } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
            g.hexagon().vectorize(x, 128);
        } else {
            g.vectorize(x, target.natural_vector_size<uint8_t>());
        }

        // Run the pipeline and verify the results are correct.
        Buffer<uint8_t> out = g.realize(W, H, target);

        for (int y = 1; y < H-1; y++) {
            for (int x = 1; x < W-1; x++) {
                uint16_t correct = kernel_sum;
                for (int ry = -1; ry <= 1; ry++) {
                    for (int rx = -1; rx <= 1; rx++) {
                        correct += static_cast<uint16_t>(in(x + rx, y + ry)) * kernel(rx, ry);
                    }
                }
                correct /= kernel_sum;
                if (correct != out(x, y)) {
                    std::cout << "out(" << x << ", " << y << ") = " << (int)out(x, y) << " instead of " << correct << "\n";
                    return -1;
                }
            }
        }
    }
    std::cout << "Success!\n";
    return 0;
}
