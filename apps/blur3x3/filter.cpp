#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "blur3x3_llvm.h"
#include "blur3x3_halide.h"
#include "blur3x3_pitchfork.h"
#include "blur3x3_rake.h"

#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 4) {
        printf("Usage: ./run input.png timing_iterations output.png\n");
        return 0;
    }

    Halide::Runtime::Buffer<uint16_t> input = load_and_convert_image(argv[1]);
    Halide::Runtime::Buffer<uint16_t> output_llvm(input.width() - 3, input.height() - 3);
    Halide::Runtime::Buffer<uint16_t> output_halide(input.width() - 3, input.height() - 3);
    Halide::Runtime::Buffer<uint16_t> output_pitchfork(input.width() - 3, input.height() - 3);
    Halide::Runtime::Buffer<uint16_t> output_rake(input.width() - 3, input.height() - 3);

    int timing_iterations = atoi(argv[2]);

    blur3x3_llvm(input, output_llvm);

    double min_t_manual = benchmark(timing_iterations, 10, [&]() {
        blur3x3_llvm(input, output_llvm);
        output_llvm.device_sync();
    });
    printf("LLVM time: %gms\n", min_t_manual * 1e3);

    blur3x3_halide(input, output_halide);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        blur3x3_halide(input, output_halide);
        output_halide.device_sync();
    });
    printf("Halide time: %gms\n", min_t_manual * 1e3);

    blur3x3_pitchfork(input, output_pitchfork);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        blur3x3_pitchfork(input, output_pitchfork);
        output_pitchfork.device_sync();
    });
    printf("Pitchfork time: %gms\n", min_t_manual * 1e3);

    blur3x3_rake(input, output_rake);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        blur3x3_rake(input, output_rake);
        output_rake.device_sync();
    });
    printf("Rake time: %gms\n", min_t_manual * 1e3);

    for (int i = 0; i < input.width() - 3; i++) {
        for (int j = 0; j < input.height() - 3; j++) {
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
