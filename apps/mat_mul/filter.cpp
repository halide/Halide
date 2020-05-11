#include <cstdio>

#include "mat_mul.h"
// #include "mat_mul_classic_auto_schedule.h"
#ifndef NO_AUTO_SCHEDULE
#include "mat_mul_auto_schedule.h"
#endif

// #include "benchmark_util.h"
#include "HalideBuffer.h"
#include "halide_benchmark.h"

using namespace Halide::Tools;
using namespace Halide::Runtime;

int main(int argc, char **argv) {
    if (argc != 1) {
        printf("Usage: %s\n", argv[0]);
        return 1;
    }

    const int matrix_size = 1536;
    const int timing_iterations = 10;

    Halide::Runtime::Buffer<float> mat_A(matrix_size, matrix_size);
    Halide::Runtime::Buffer<float> mat_B(matrix_size, matrix_size);
    Halide::Runtime::Buffer<float> output(matrix_size, matrix_size);

    // init randomly
    for (int iy = 0; iy < matrix_size; iy++) {
        for (int ix = 0; ix < matrix_size; ix++) {
            mat_A(ix, iy) = (rand() % 256) / 256.0f;
            mat_B(ix, iy) = (rand() % 256) / 256.0f;
        }
    }

    /*
    three_way_bench(
        [&]() { mat_mul(mat_A, mat_B, output); },
        [&]() { mat_mul_classic_auto_schedule(mat_A, mat_B, output); },
        [&]() { mat_mul_auto_schedule(mat_A, mat_B, output); }
    );
    */

    // Manually-tuned version
    double min_t_manual = benchmark(timing_iterations, 10, [&]() {
        mat_mul(mat_A, mat_B, output);
        output.device_sync();
    });
    printf("Manually-tuned time: %gms\n", min_t_manual * 1e3);

#ifndef NO_AUTO_SCHEDULE
    // Auto-scheduled version
    double min_t_auto = benchmark(timing_iterations, 10, [&]() {
        mat_mul_auto_schedule(mat_A, mat_B, output);
        output.device_sync();
    });
    printf("Auto-scheduled time: %gms\n", min_t_auto * 1e3);
#endif

    return 0;
}
