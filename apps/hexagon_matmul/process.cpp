#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>
#include <malloc.h>

#include "halide_benchmark.h"

#include "matmul.h"

#include "HalideBuffer.h"

template <typename T>
T clamp(T x, T min, T max) {
    if (x < min) x = min;
    if (x > max) x = max;
    return x;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        printf("Usage: %s timing_iterations M N K\n", argv[0]);
        return 0;
    }

    int iterations = atoi(argv[1]);

    const int M = atoi(argv[2]);
    const int N = atoi(argv[3]);
    const int K = atoi(argv[4]);

    Halide::Runtime::Buffer<uint8_t> mat_a(nullptr, N, M);
    Halide::Runtime::Buffer<uint8_t> mat_b(nullptr, K, N);
    Halide::Runtime::Buffer<uint32_t> mat_ab(nullptr, K, M);

#ifdef HALIDE_RUNTIME_HEXAGON
    // Hexagon's device_malloc implementation will also set the host
    // pointer if it is null, giving a zero copy buffer.
    mat_a.device_malloc(halide_hexagon_device_interface());
    mat_b.device_malloc(halide_hexagon_device_interface());
    mat_ab.device_malloc(halide_hexagon_device_interface());
#else
    mat_a.allocate();
    mat_b.allocate();
    mat_ab.allocate();
#endif

    // Fill the input buffer with random data.
    mat_a.for_each_value([&](uint8_t &x) {
        x = static_cast<uint8_t>(rand());
    });
    mat_b.for_each_value([&](uint8_t &x) {
        x = static_cast<uint8_t>(rand());
    });

#ifdef HALIDE_RUNTIME_HEXAGON
    // To avoid the cost of powering HVX on in each call of the
    // pipeline, power it on once now. Also, set Hexagon performance to turbo.
    halide_hexagon_set_performance_mode(nullptr, halide_hexagon_power_turbo);
    halide_hexagon_power_hvx_on(nullptr);
#endif

    printf("Running pipeline...\n");
    double time = Halide::Tools::benchmark(iterations, 1, [&]() {
        int result = matmul(mat_a, mat_b, mat_ab);
        if (result != 0) {
            printf("pipeline failed! %d\n", result);
        }
    });

    printf("Done, time: %g s\n", time);

#ifdef HALIDE_RUNTIME_HEXAGON
    // We're done with HVX, power it off, and reset the performance mode
    // to default to save power.
    halide_hexagon_power_hvx_off(nullptr);
    halide_hexagon_set_performance_mode(nullptr, halide_hexagon_power_default);
#endif

    // Copy the output back to the host. If the buffer is zero-copy (as
    // it should be on a real device), this will be a no-op.
    mat_ab.copy_to_host();

    // Validate that the algorithm did what we expect.
    mat_ab.for_each_element([&](int x, int y) {
        // This reference implementation is very slow, so only check a subset of the result.
        if ((y * N + x) % 100 != 0) {
            return;
        }
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
