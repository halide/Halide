#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "matmul_llvm.h"
#include "matmul_halide.h"
#include "matmul_pitchfork.h"
#include "matmul_rake.h"

#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 5) {
        printf("Usage: ./run N M K timing_iterations\n");
        return -1;
    }

    const int N = atoi(argv[1]);
    const int M = atoi(argv[2]);
    const int K = atoi(argv[3]);
    const int timing_iterations = atoi(argv[4]);

    Halide::Runtime::Buffer<uint8_t> mat_a(N, M);
    Halide::Runtime::Buffer<uint8_t> mat_b(M, K);
    Halide::Runtime::Buffer<int32_t> bias(N);

    Halide::Runtime::Buffer<uint8_t> output_llvm(N, K);
    Halide::Runtime::Buffer<uint8_t> output_halide(N, K);
    Halide::Runtime::Buffer<uint8_t> output_pitchfork(N, K);
    Halide::Runtime::Buffer<uint8_t> output_rake(N, K);


    matmul_llvm(mat_a, mat_b, bias, 0, 0, 65536, 1, 0, 5, 250, output_llvm);

    double min_t_manual = benchmark(timing_iterations, 10, [&]() {
        matmul_llvm(mat_a, mat_b, bias, 0, 0, 65536, 1, 0, 5, 250, output_llvm);
        output_llvm.device_sync();
    });
    printf("LLVM time: %gms\n", min_t_manual * 1e3);

    matmul_halide(mat_a, mat_b, bias, 0, 0, 65536, 1, 0, 5, 250, output_halide);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        matmul_halide(mat_a, mat_b, bias, 0, 0, 65536, 1, 0, 5, 250, output_halide);
        output_halide.device_sync();
    });
    printf("Halide time: %gms\n", min_t_manual * 1e3);

    matmul_pitchfork(mat_a, mat_b, bias, 0, 0, 65536, 1, 0, 5, 250, output_pitchfork);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        matmul_pitchfork(mat_a, mat_b, bias, 0, 0, 65536, 1, 0, 5, 250, output_pitchfork);
        output_pitchfork.device_sync();
    });
    printf("Pitchfork time: %gms\n", min_t_manual * 1e3);

    matmul_rake(mat_a, mat_b, bias, 0, 0, 65536, 1, 0, 5, 250, output_rake);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        matmul_rake(mat_a, mat_b, bias, 0, 0, 65536, 1, 0, 5, 250, output_rake);
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
