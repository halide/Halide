#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>
#include <malloc.h>

#include <inttypes.h>
#include "halide_benchmark.h"

#include "MatrixMultiply_cpu.h"
#include "MatrixMultiply_hvx64.h"
#include "MatrixMultiply_hvx128.h"

#include "HalideRuntimeHexagonHost.h"
#include "HalideBuffer.h"

int32_t SaturatingRoundingDoublingHighMultiply(int32_t a, int32_t b) {
    int64_t a_wide = a;
    int64_t b_wide = b;
    int64_t ab_wide = a_wide * b_wide;
    int64_t nudge = 1 << 30;
    int64_t result = (ab_wide + nudge) >> 31;
    result = std::max(result, (int64_t)std::numeric_limits<int32_t>::min());
    result = std::min(result, (int64_t)std::numeric_limits<int32_t>::max());
    return (int32_t)result;
}

int32_t RoundingShiftRight(int32_t x, int32_t shift) {
    // Shift must satisfy 0 <= shift <= 31
    int32_t mask = ((1ll << shift) - 1);
    int32_t remainder = x & mask;
    int32_t threshold = (mask >> 1) + (x < 0 ? 1 : 0);
    return (x >> shift) + (remainder > threshold ? 1 : 0);
}

int32_t MultiplyByQuantizedMultiplier(int32_t x, int32_t q, int32_t shift) {
    return RoundingShiftRight(SaturatingRoundingDoublingHighMultiply(x, q), shift);
}

int main(int argc, char **argv) {
    if (argc < 6) {
        printf("Usage: %s (cpu|hvx64) timing_iterations M N K [mat_a_offset mat_b_offset output_multiplier output_shift output_offset output_min output_max]\n", argv[0]);
        return 0;
    }

    int (*pipeline)(halide_buffer_t *, halide_buffer_t*, halide_buffer_t*, int16_t, int16_t,
                    int, int, int, uint8_t, uint8_t, halide_buffer_t*);
    int k_alignment = 1;
    if (strcmp(argv[1], "cpu") == 0) {
        pipeline = MatrixMultiply_cpu;
        printf("Using CPU schedule\n");
        k_alignment = 32;
    } else if (strcmp(argv[1], "hvx64") == 0) {
        pipeline = MatrixMultiply_hvx64;
        printf("Using HVX 64 schedule\n");
        k_alignment = 64;
    } else if (strcmp(argv[1], "hvx128") == 0) {
        pipeline = MatrixMultiply_hvx128;
        printf("Using HVX 128 schedule\n");
        k_alignment = 128;
    } else {
        printf("Unknown schedule, valid schedules are cpu, hvx64, or hvx128\n");
        return -1;
    }

    int iterations = atoi(argv[2]);

    int M = atoi(argv[3]);
    int N = atoi(argv[4]);
    int K = atoi(argv[5]);

    // Align the dimensions as required.
    M = (M + 3) & ~3;
    N = (N + 3) & ~3;
    K = (K + 3) & ~3;

    K = (K + k_alignment - 1) & ~(k_alignment - 1);

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
        int32_t ab_xy = bias(x);
        for (int k = 0; k < N; k++) {
            int32_t a_ky = (int32_t)mat_a(k, y) + mat_a_offset;
            int32_t b_xk = (int32_t)mat_b(x, k) + mat_b_offset;
            ab_xy += a_ky * b_xk;
        }

        int32_t output = MultiplyByQuantizedMultiplier(ab_xy, output_multiplier, output_shift);
        output += output_offset;
        output = std::max(output, (int32_t)output_min);
        output = std::min(output, (int32_t)output_max);
        if (output != mat_ab(x, y)) {
            printf("Mismatch at %d %d: %d != %d\n", x, y, output, mat_ab(x, y));
            abort();
        }
    });

    printf("Success!\n");
    return 0;
}
