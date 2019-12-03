#include <assert.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#include "halide_benchmark.h"

#include "MatrixMultiply.h"
#include "common_reference.h"

#include "HalideBuffer.h"

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: %s M N K [mat_a_offset mat_b_offset output_multiplier output_shift output_offset output_min output_max]\n", argv[0]);
        return 0;
    }

    int M = atoi(argv[1]);
    int N = atoi(argv[2]);
    int K = atoi(argv[3]);

    printf("Benchmarking %dx%d * %dx%d\n", M, N, N, K);

    // TODO: We could reduce k_alignment on some targets. 128 is
    // conservative to enable Hexagon with 128-byte vectors.
    int k_alignment = 128;

    // Align the dimensions as required.
    M = (M + 3) & ~3;
    N = (N + 3) & ~3;
    K = (K + 3) & ~3;
    K = (K + k_alignment - 1) & ~(k_alignment - 1);

    printf("Aligned to %dx%d * %dx%d\n", M, N, N, K);

    // Hexagon's device_malloc implementation will also set the host
    // pointer if it is null, giving a zero copy buffer.
    Halide::Runtime::Buffer<uint8_t> mat_a(nullptr, N, M);
    Halide::Runtime::Buffer<uint8_t> mat_b(nullptr, K, N);
    Halide::Runtime::Buffer<int32_t> bias(nullptr, K);

    // These parameters (-128 matrix offsets, +128 output offset,
    // output shift of 8) lead to reasonable values for testing in
    // most cases (expected value of the input matrices is ~0,
    // expected value of the product is ~0).
    int16_t mat_a_offset = -128;
    int16_t mat_b_offset = -128;
    int output_multiplier = 1 << 30;
    int output_shift = 8;
    int output_offset = 128;
    uint8_t output_min = 0;
    uint8_t output_max = 255;
    if (argc > 6) mat_a_offset = atoi(argv[4]);
    if (argc > 7) mat_b_offset = atoi(argv[5]);
    if (argc > 8) output_multiplier = atoi(argv[6]);
    if (argc > 9) output_shift = atoi(argv[7]);
    if (argc > 10) output_offset = atoi(argv[8]);
    if (argc > 11) output_min = atoi(argv[9]);
    if (argc > 12) output_max = atoi(argv[10]);

    Halide::Runtime::Buffer<uint8_t> mat_ab(nullptr, K, M);

#ifdef HALIDE_RUNTIME_HEXAGON
    mat_a.device_malloc(halide_hexagon_device_interface());
    mat_b.device_malloc(halide_hexagon_device_interface());
    bias.device_malloc(halide_hexagon_device_interface());
    mat_ab.device_malloc(halide_hexagon_device_interface());
#else
    mat_a.allocate();
    mat_b.allocate();
    bias.allocate();
    mat_ab.allocate();
#endif

    mat_a.for_each_value([](uint8_t &x) {
        x = static_cast<uint8_t>(rand());
    });
    mat_b.for_each_value([](uint8_t &x) {
        x = static_cast<uint8_t>(rand());
    });
    bias.for_each_value([](int32_t &x) {
        x = static_cast<int16_t>(rand());
    });

#ifdef HALIDE_RUNTIME_HEXAGON
    // To avoid the cost of powering HVX on in each call of the
    // pipeline, power it on once now. Also, set Hexagon performance to turbo.
    halide_hexagon_set_performance_mode(nullptr, halide_hexagon_power_turbo);
    halide_hexagon_power_hvx_on(nullptr);
#endif

    printf("Running pipeline...\n");
    double time = Halide::Tools::benchmark([&]() {
        int result = MatrixMultiply(mat_a, mat_b, bias, mat_a_offset, mat_b_offset,
                                    output_multiplier, output_shift, output_offset,
                                    output_min, output_max, mat_ab);
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
        int32_t ab_xy = bias(x);
        for (int k = 0; k < N; k++) {
            int32_t a_ky = (int32_t)mat_a(k, y) + mat_a_offset;
            int32_t b_xk = (int32_t)mat_b(x, k) + mat_b_offset;
            ab_xy += a_ky * b_xk;
        }

        int32_t output = multiply_quantized_multiplier_reference(ab_xy, output_multiplier, output_shift);
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
