#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>
#include <malloc.h>

#include "../support/benchmark.h"

#include "pipeline_cpu.h"
#include "pipeline_hvx64.h"
#include "pipeline_hvx128.h"

#include "HalideRuntimeHexagonHost.h"
#include "HalideBuffer.h"

template <typename T>
T clamp(T x, T min, T max) {
    if (x < min) x = min;
    if (x > max) x = max;
    return x;
}

int main(int argc, char **argv) {
    if (argc < 6) {
        printf("Usage: %s (cpu|hvx64) timing_iterations M N K\n", argv[0]);
        return 0;
    }

    int (*pipeline)(buffer_t *, buffer_t*, buffer_t*);
    if (strcmp(argv[1], "cpu") == 0) {
        pipeline = pipeline_cpu;
        printf("Using CPU schedule\n");
    } else if (strcmp(argv[1], "hvx64") == 0) {
        pipeline = pipeline_hvx64;
        printf("Using HVX 64 schedule\n");
    } else if (strcmp(argv[1], "hvx128") == 0) {
        pipeline = pipeline_hvx128;
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
    Halide::Runtime::Buffer<uint32_t> mat_ab(nullptr, K, M);

    mat_a.device_malloc(halide_hexagon_device_interface());
    mat_b.device_malloc(halide_hexagon_device_interface());
    mat_ab.device_malloc(halide_hexagon_device_interface());

    // Fill the input buffer with random data.
    mat_a.for_each_value([&](uint8_t &x) {
        x = static_cast<uint8_t>(rand());
    });
    mat_b.for_each_value([&](uint8_t &x) {
        x = static_cast<uint8_t>(rand());
    });

    // To avoid the cost of powering HVX on in each call of the
    // pipeline, power it on once now.
    halide_hexagon_set_performance_mode(nullptr, halide_hvx_power_turbo);
    halide_hexagon_power_hvx_on(nullptr);

    printf("Running pipeline...\n");
    double time = benchmark(iterations, 1, [&]() {
        int result = pipeline(mat_a, mat_b, mat_ab);
        if (result != 0) {
            printf("pipeline failed! %d\n", result);
        }
    });

    printf("Done, time: %g s\n", time);

    // We're done with HVX, power it off.
    halide_hexagon_power_hvx_off(nullptr);

    // Validate that the algorithm did what we expect.
    mat_ab.for_each_element([&](int x, int y) {
        uint32_t ab_xy = 0;
        for (int k = 0; k < K; k++) {
            ab_xy += static_cast<uint32_t>(mat_a(k, y))*static_cast<uint32_t>(mat_b(x, k));
        }

        if (ab_xy != mat_ab(x, y)) {
            printf("Mismatch at %d %d: %d != %d\n", x, y, ab_xy, mat_ab(x, y));
            abort();
        }

    });

    printf("Success!\n");
    return 0;
}
