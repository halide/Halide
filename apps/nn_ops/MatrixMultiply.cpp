#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>
#include <malloc.h>

#include "halide_benchmark.h"

#include "MatrixMultiply_cpu.h"
#include "MatrixMultiply_hvx64.h"
#include "MatrixMultiply_hvx128.h"

#include "HalideRuntimeHexagonHost.h"
#include "HalideBuffer.h"

template <typename T, typename U>
T clamp(T x, U min, U max) {
    if (x < min) x = min;
    if (x > max) x = max;
    return x;
}

int main(int argc, char **argv) {
    if (argc < 6) {
        printf("Usage: %s (cpu|hvx64) timing_iterations M N K [mat_a_offset mat_b_offset output_multiplier output_shift output_offset output_min output_max]\n", argv[0]);
        return 0;
    }

    int (*pipeline)(halide_buffer_t *, halide_buffer_t*, halide_buffer_t*, int16_t, int16_t,
                    int, int, int, uint8_t, uint8_t, halide_buffer_t*);
    if (strcmp(argv[1], "cpu") == 0) {
        pipeline = MatrixMultiply_cpu;
        printf("Using CPU schedule\n");
    } else if (strcmp(argv[1], "hvx64") == 0) {
        pipeline = MatrixMultiply_hvx64;
        printf("Using HVX 64 schedule\n");
    } else if (strcmp(argv[1], "hvx128") == 0) {
        pipeline = MatrixMultiply_hvx128;
        printf("Using HVX 128 schedule\n");
    } else {
        printf("Unknown schedule, valid schedules are cpu, hvx64, or hvx128\n");
        return -1;
    }

    int iterations = atoi(argv[2]);

    const int M = atoi(argv[3]);
    const int N = atoi(argv[4]);
    const int K = atoi(argv[5]);

    // Hexagon's device_malloc implementation will also set the host
    // pointer if it is null, giving a zero copy buffer.
    Halide::Runtime::Buffer<uint8_t> mat_a(nullptr, N, M);
    Halide::Runtime::Buffer<uint8_t> mat_b(nullptr, K, N);
    Halide::Runtime::Buffer<int32_t> bias(nullptr, K);

    int16_t mat_a_offset = 0;
    int16_t mat_b_offset = 0;
    int output_multiplier = 1 << 30;
    int output_shift = 14;
    int output_offset = 0;
    uint8_t output_min = 0;
    uint8_t output_max = 255;
    if (argc > 6) mat_a_offset = atoi(argv[6]);
    if (argc > 7) mat_b_offset = atoi(argv[7]);
    if (argc > 8) output_multiplier = atoi(argv[8]);
    if (argc > 9) output_shift = atoi(argv[9]);
    if (argc > 10) output_offset = atoi(argv[10]);
    if (argc > 11) output_min = atoi(argv[11]);
    if (argc > 12) output_max = atoi(argv[12]);

    Halide::Runtime::Buffer<uint8_t> mat_ab(nullptr, K, M);

    mat_a.device_malloc(halide_hexagon_device_interface());
    mat_b.device_malloc(halide_hexagon_device_interface());
    bias.device_malloc(halide_hexagon_device_interface());
    mat_ab.device_malloc(halide_hexagon_device_interface());

    mat_a.for_each_value([](uint8_t &x) {
        x = static_cast<uint8_t>(rand());
    });
    mat_b.for_each_value([](uint8_t &x) {
        x = static_cast<uint8_t>(rand());
    });
    bias.for_each_value([](int32_t &x) {
        x = static_cast<int16_t>(rand());
    });

    // To avoid the cost of powering HVX on in each call of the
    // pipeline, power it on once now. Also, set Hexagon performance to turbo.
    halide_hexagon_set_performance_mode(nullptr, halide_hexagon_power_turbo);
    halide_hexagon_power_hvx_on(nullptr);

    printf("Running pipeline...\n");
    double time = Halide::Tools::benchmark(iterations, 1, [&]() {
        int result = pipeline(mat_a, mat_b, bias, mat_a_offset, mat_b_offset,
                              output_multiplier, output_shift, output_offset,
                              output_min, output_max, mat_ab);
        if (result != 0) {
            printf("pipeline failed! %d\n", result);
        }
    });

    printf("Done, time: %g s\n", time);

    // We're done with HVX, power it off, and reset the performance mode
    // to default to save power.
    halide_hexagon_power_hvx_off(nullptr);
    halide_hexagon_set_performance_mode(nullptr, halide_hexagon_power_default);

    // Copy the output back to the host. If the buffer is zero-copy (as
    // it should be on a real device), this will be a no-op.
    mat_ab.copy_to_host();

    // Validate that the algorithm did what we expect.
    mat_ab.for_each_element([&](int x, int y) {
        // This reference implementation is very slow, so only check a subset of the result.
        if ((y * N + x) % 100 != 0) {
            return;
        }
        int64_t ab_xy = bias(x);
        for (int k = 0; k < K; k++) {
            int32_t a_ky = static_cast<int32_t>(mat_a(k, y)) + mat_a_offset;
            int32_t b_xk = static_cast<int32_t>(mat_b(x, k)) + mat_b_offset;
            ab_xy += a_ky * b_xk;
        }

        int32_t multiplied = clamp((static_cast<int64_t>(ab_xy) * output_multiplier + (1 << 30)) >> 31,
                                   std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max());
        int64_t round = output_shift <= 0 ? 0 : 1 << (output_shift - 1);
        uint8_t output = clamp(((multiplied + round) >> output_shift) + output_offset, output_min, output_max);
        if (output != mat_ab(x, y)) {
            printf("Mismatch at %d %d: %d != %d\n", x, y, output, mat_ab(x, y));
            abort();
        }
    });

    printf("Success!\n");
    return 0;
}
