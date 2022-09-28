#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "conv3x3_a16_llvm.h"
#include "conv3x3_a16_halide.h"
#include "conv3x3_a16_pitchfork.h"
#include "conv3x3_a16_rake.h"

#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 4) {
        printf("Usage: ./run N M timing_iterations\n");
        return -1;
    }

    const int N = atoi(argv[1]);
    const int M = atoi(argv[2]);
    const int timing_iterations = atoi(argv[3]);

    Halide::Runtime::Buffer<uint8_t> input(N, M);
    Halide::Runtime::Buffer<int8_t> mask(3, 3);

    Halide::Runtime::Buffer<uint8_t> output_llvm(N, M);
    Halide::Runtime::Buffer<uint8_t> output_halide(N, M);
    Halide::Runtime::Buffer<uint8_t> output_pitchfork(N, M);
    Halide::Runtime::Buffer<uint8_t> output_rake(N, M);

    conv3x3_a16_llvm(input, mask, output_llvm);

    double min_t_manual = benchmark(timing_iterations, 10, [&]() {
         conv3x3_a16_llvm(input, mask, output_llvm);
        output_llvm.device_sync();
    });
    printf("LLVM time: %gms\n", min_t_manual * 1e3);

    conv3x3_a16_halide(input, mask, output_halide);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        conv3x3_a16_halide(input, mask, output_halide);
        output_halide.device_sync();
    });
    printf("Halide time: %gms\n", min_t_manual * 1e3);

    conv3x3_a16_pitchfork(input, mask, output_pitchfork);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        conv3x3_a16_pitchfork(input, mask, output_pitchfork);
        output_pitchfork.device_sync();
    });
    printf("Pitchfork time: %gms\n", min_t_manual * 1e3);

    conv3x3_a16_rake(input, mask, output_rake);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        conv3x3_a16_rake(input, mask, output_rake);
        output_rake.device_sync();
    });
    printf("Rake time: %gms\n", min_t_manual * 1e3);

    for (int i = 0; i < N; i++) {
        for (int j = 0; j < M; j++) {
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
