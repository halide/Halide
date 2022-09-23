#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "softmax_llvm.h"
#include "softmax_halide.h"
#include "softmax_pitchfork.h"
#include "softmax_rake.h"

#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 4) {
        printf("Usage: ./run input.png timing_iterations output.png\n");
        return 0;
    }

    Halide::Runtime::Buffer<uint8_t> input = load_and_convert_image(argv[1]);
    Halide::Runtime::Buffer<uint8_t> output_llvm(input.width(), input.height());
    Halide::Runtime::Buffer<uint8_t> output_halide(input.width(), input.height());
    Halide::Runtime::Buffer<uint8_t> output_pitchfork(input.width(), input.height());
    Halide::Runtime::Buffer<uint8_t> output_rake(input.width(), input.height());

    int timing_iterations = atoi(argv[2]);

    // input, beta_multiplier, beta_shift, output_zero, output_multiplier, output_shift, output
    softmax_llvm(input, 16, 4, 5, 10000, 1, output_llvm);

    double min_t_manual = benchmark(timing_iterations, 10, [&]() {
        softmax_llvm(input, 16, 4, 5, 10000, 1, output_llvm);
        output_llvm.device_sync();
    });
    printf("LLVM time: %gms\n", min_t_manual * 1e3);

    softmax_halide(input, 16, 4, 5, 10000, 1, output_halide);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        softmax_halide(input, 16, 4, 5, 10000, 1, output_halide);
        output_halide.device_sync();
    });
    printf("Halide time: %gms\n", min_t_manual * 1e3);

    softmax_pitchfork(input, 16, 4, 5, 10000, 1, output_pitchfork);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        softmax_pitchfork(input, 16, 4, 5, 10000, 1, output_pitchfork);
        output_pitchfork.device_sync();
    });
    printf("Pitchfork time: %gms\n", min_t_manual * 1e3);

    softmax_rake(input, 16, 4, 5, 10000, 1, output_rake);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        softmax_rake(input, 16, 4, 5, 10000, 1, output_rake);
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

    convert_and_save_image(output_pitchfork, argv[3]);

    printf("Success!\n");
    return 0;
}
