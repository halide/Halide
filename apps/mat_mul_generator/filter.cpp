#include <cstdio>

#include "mat_mul.h"
#include "mat_mul_classic_auto_schedule.h"
#include "mat_mul_auto_schedule.h"

#include "benchmark_util.h"
#include "HalideBuffer.h"

int main(int argc, char **argv) {
    if (argc != 1) {
        printf("Usage: %s\n", argv[0]);
        return 1;
    }

    const int matrix_size = 992;

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

    three_way_bench(
        [&]() { mat_mul(mat_A, mat_B, output); output.device_sync(); },
        [&]() { mat_mul_classic_auto_schedule(mat_A, mat_B, output); output.device_sync(); },
        [&]() { mat_mul_auto_schedule(mat_A, mat_B, output); output.device_sync(); }
    );

    return 0;
}
