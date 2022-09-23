#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "fully_connected_llvm.h"
#include "fully_connected_halide.h"
#include "fully_connected_pitchfork.h"
#include "fully_connected_rake.h"

#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 5) {
        printf("Usage: ./run N M K timing_iterations\n");
        return 0;
    }

    const int N = atoi(argv[1]);
    const int M = atoi(argv[2]);
    const int K = atoi(argv[3]);
    const int timing_iterations = atoi(argv[4]);

    Halide::Runtime::Buffer<uint8_t> input(N, M);
    Halide::Runtime::Buffer<uint8_t> filter(M, K);
    Halide::Runtime::Buffer<int32_t> bias(N);

    Halide::Runtime::Buffer<uint8_t> output_llvm(N, K);
    Halide::Runtime::Buffer<uint8_t> output_halide(N, K);
    Halide::Runtime::Buffer<uint8_t> output_pitchfork(N, K);
    Halide::Runtime::Buffer<uint8_t> output_rake(N, K);

    fully_connected_llvm(input, 3, filter, 5, bias, 7, 32767, 1, 5, 250, output_llvm);

    double min_t_manual = benchmark(timing_iterations, 10, [&]() {
        fully_connected_llvm(input, 3, filter, 5, bias, 7, 32767, 1, 5, 250, output_llvm);
        output_llvm.device_sync();
    });
    printf("LLVM time: %gms\n", min_t_manual * 1e3);

    fully_connected_halide(input, 3, filter, 5, bias, 7, 32767, 1, 5, 250, output_halide);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        fully_connected_halide(input, 3, filter, 5, bias, 7, 32767, 1, 5, 250, output_halide);
        output_halide.device_sync();
    });
    printf("Halide time: %gms\n", min_t_manual * 1e3);

    fully_connected_pitchfork(input, 3, filter, 5, bias, 7, 32767, 1, 5, 250, output_pitchfork);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        fully_connected_pitchfork(input, 3, filter, 5, bias, 7, 32767, 1, 5, 250, output_pitchfork);
        output_pitchfork.device_sync();
    });
    printf("Pitchfork time: %gms\n", min_t_manual * 1e3);

    fully_connected_rake(input, 3, filter, 5, bias, 7, 32767, 1, 5, 250, output_rake);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        fully_connected_rake(input, 3, filter, 5, bias, 7, 32767, 1, 5, 250, output_rake);
        output_rake.device_sync();
    });
    printf("Rake time: %gms\n", min_t_manual * 1e3);

    for (int i = 0; i < N; i++) {
        for (int j = 0; j < K; j++) {
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
