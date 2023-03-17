#include "Halide.h"
#include <iostream>
#include <stdio.h>

using namespace Halide;
using namespace Halide::ConciseCasts;
using namespace Halide::Internal;

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
    Buffer<int8_t> kernel(3, 3);
    kernel.set_min(-1, -1);
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            kernel(x, y) = rand() % 8 - 4;
        }
    }

    Var x("x"), y("y"), xi("xi"), yi("yi");
    RDom r(-1, 3, -1, 3);

    // Boundary condition.
    Func input = BoundaryConditions::repeat_edge(in);
    input.compute_root();

    // Test a widening reduction, followed by a narrowing.
    {
        Func f;
        f(x, y) = u8_sat(sum(i16(input(x + r.x, y + r.y)) * kernel(r.x, r.y)) / 16);

        // Schedule.
        Target target = get_jit_target_from_environment();
        if (target.has_gpu_feature()) {
            f.gpu_tile(x, y, xi, yi, 16, 16);
        } else if (target.has_feature(Target::HVX)) {
            f.hexagon().vectorize(x, 128);
        } else {
            f.vectorize(x, target.natural_vector_size<uint8_t>());
        }

        // Run the pipeline and verify the results are correct.
        Buffer<uint8_t> out = f.realize({W, H}, target);

        for (int y = 1; y < H - 1; y++) {
            for (int x = 1; x < W - 1; x++) {
                int16_t correct = 0;
                for (int ry = -1; ry <= 1; ry++) {
                    for (int rx = -1; rx <= 1; rx++) {
                        correct += static_cast<int16_t>(in(x + rx, y + ry)) * kernel(rx, ry);
                    }
                }
                correct = std::min(std::max(correct / 16, 0), 255);
                if (correct != out(x, y)) {
                    std::cout << "out(" << x << ", " << y << ") = " << (int)out(x, y) << " instead of " << correct << "\n";
                    return 1;
                }
            }
        }
    }

    // Test a tuple reduction with widening, followed by narrowing the result.
    {
        Func f;
        f(x, y) = {i16(0), i8(0)};
        f(x, y) = {
            f(x, y)[0] + i16(input(x + r.x, y + r.y)) * kernel(r.x, r.y),
            f(x, y)[1] + kernel(r.x, r.y),
        };

        Func g;
        g(x, y) = u8_sat((f(x, y)[0] + f(x, y)[1]) / 16);

        // Schedule.
        Target target = get_jit_target_from_environment();
        if (target.has_gpu_feature()) {
            g.gpu_tile(x, y, xi, yi, 16, 16);
        } else if (target.has_feature(Target::HVX)) {
            g.hexagon().vectorize(x, 128);
        } else {
            g.vectorize(x, target.natural_vector_size<uint8_t>());
        }

        // Run the pipeline and verify the results are correct.
        Buffer<uint8_t> out = g.realize({W, H}, target);

        for (int y = 1; y < H - 1; y++) {
            for (int x = 1; x < W - 1; x++) {
                int16_t correct = 0;
                for (int ry = -1; ry <= 1; ry++) {
                    for (int rx = -1; rx <= 1; rx++) {
                        correct += static_cast<int16_t>(in(x + rx, y + ry)) * kernel(rx, ry);
                        correct += kernel(rx, ry);
                    }
                }
                correct = std::min(std::max(correct / 16, 0), 255);
                if (correct != out(x, y)) {
                    std::cout << "out(" << x << ", " << y << ") = " << (int)out(x, y) << " instead of " << correct << "\n";
                    return 1;
                }
            }
        }
    }

    // Test a widening, followed by a narrowing reduction with an
    // unaligned output. This triggered a bug in EliminateInterleaves
    // on Hexagon.
    {
        Func f;
        f(x, y) = i16(input(x, y));

        Func g;
        g(x, y) = u8_sat((f(x, y) + f(x + 1, y)) / 2);

        // Schedule.
        Target target = get_jit_target_from_environment();
        if (target.has_gpu_feature()) {
            g.gpu_tile(x, y, xi, yi, 16, 16);
        } else if (target.has_feature(Target::HVX)) {
            g.hexagon().vectorize(x, 128);
            f.compute_at(g, y).vectorize(x, 128, TailStrategy::RoundUp);
        } else {
            g.vectorize(x, target.natural_vector_size<uint8_t>());
        }

        g.output_buffer().dim(0).set_min(0).set_extent(W - 2);
        g.output_buffer().dim(1).set_min(0).set_extent(H);

        // Run the pipeline and verify the results are correct.
        Buffer<uint8_t> out = g.realize({W - 2, H}, target);

        for (int y = 1; y < H - 1; y++) {
            for (int x = 0; x < W - 3; x++) {
                uint8_t correct = (static_cast<int16_t>(in(x, y)) + in(x + 1, y)) / 2;
                if (correct != out(x, y)) {
                    std::cout << "out(" << x << ", " << y << ") = " << (int)out(x, y) << " instead of " << (int)correct << "\n";
                    return 1;
                }
            }
        }
    }

    std::cout << "Success!\n";
    return 0;
}
