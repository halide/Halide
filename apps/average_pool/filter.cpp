#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "average_pool_llvm.h"
#include "average_pool_halide.h"
#include "average_pool_pitchfork.h"
#include "average_pool_rake.h"

#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 6) {
        printf("Usage: ./run c_dim x_dim y_dim b_dim timing_iterations\n");
        return 0;
    }

    const int c_dim = atoi(argv[1]);
    const int x_dim = atoi(argv[2]);
    const int y_dim = atoi(argv[3]);
    const int b_dim = atoi(argv[4]);
    const int timing_iterations = atoi(argv[5]);

    Halide::Runtime::Buffer<uint8_t> input(c_dim, x_dim, y_dim, b_dim);
    Halide::Runtime::Buffer<uint8_t> output_llvm(c_dim, x_dim, y_dim, b_dim);
    Halide::Runtime::Buffer<uint8_t> output_halide(c_dim, x_dim, y_dim, b_dim);
    Halide::Runtime::Buffer<uint8_t> output_pitchfork(c_dim, x_dim, y_dim, b_dim);
    Halide::Runtime::Buffer<uint8_t> output_rake(c_dim, x_dim, y_dim, b_dim);

    average_pool_llvm(input, 2, 2, 8, 8, 5, 225, output_llvm);

    double min_t_manual = benchmark(timing_iterations, 10, [&]() {
        average_pool_llvm(input, 2, 2, 8, 8, 5, 225, output_llvm);
        output_llvm.device_sync();
    });
    printf("LLVM time: %gms\n", min_t_manual * 1e3);

    average_pool_halide(input, 2, 2, 8, 8, 5, 225, output_halide);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        average_pool_halide(input, 2, 2, 8, 8, 5, 225, output_halide);
        output_halide.device_sync();
    });
    printf("Halide time: %gms\n", min_t_manual * 1e3);

    average_pool_pitchfork(input, 2, 2, 8, 8, 5, 225, output_pitchfork);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        average_pool_pitchfork(input, 2, 2, 8, 8, 5, 225, output_pitchfork);
        output_pitchfork.device_sync();
    });
    printf("Pitchfork time: %gms\n", min_t_manual * 1e3);

    average_pool_rake(input, 2, 2, 8, 8, 5, 225, output_rake);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        average_pool_rake(input, 2, 2, 8, 8, 5, 225, output_rake);
        output_rake.device_sync();
    });
    printf("Rake time: %gms\n", min_t_manual * 1e3);

    for (int i = 0; i < input.width(); i++) {
        for (int j = 0; j < input.height(); j++) {
            if (output_llvm(i, j) != output_halide(i, j)) {
                std::cerr << "Halide failure at pixel i=" << i << ", j=" << j << ": "
                          << (int)output_llvm(i, j) << " != " << (int)output_halide(i, j) << "\n";
                return -1;
            }

            if (output_llvm(i, j) != output_pitchfork(i, j)) {
                std::cerr << "Pitchfork failure at pixel i=" << i << ", j=" << j << ": "
                          << (int)output_llvm(i, j) << " != " << (int)output_pitchfork(i, j) << "\n";
                return -1;
            }

            if (output_llvm(i, j) != output_rake(i, j)) {
                std::cerr << "Rake failure at pixel i=" << i << ", j=" << j << ": "
                          << (int)output_llvm(i, j) << " != " << (int)output_rake(i, j) << "\n";
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
