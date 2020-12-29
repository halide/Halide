#include <assert.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#include <limits>

#include "halide_benchmark.h"

#include "MaxPool.h"

#include "HalideBuffer.h"

int clamp(int x, int low, int high) {
    return std::min(std::max(x, low), high);
}

int main(int argc, char **argv) {
    if (argc < 5) {
        printf("Usage: %s C W H N [stride pad_width pad_height filter_width filter_height output_min output_max]\n", argv[0]);
        return 0;
    }

    int C = atoi(argv[1]);
    int W = atoi(argv[2]);
    int H = atoi(argv[3]);
    int N = atoi(argv[4]);

    printf("Benchmarking %dx%dx%dx%d\n", C, W, H, N);

    // TODO(vksnk): do we need to align C, W, H, N?

    // Hexagon's device_malloc implementation will also set the host
    // pointer if it is null, giving a zero copy buffer.
    Halide::Runtime::Buffer<uint8_t> input_tensor(nullptr, C, W, H, N);

    // These parameters lead to reasonable values for testing in
    // most cases (expected value of the input matrices is ~0,
    // expected value of the product is ~0).
    int stride = 1;
    int pad_width = 0;
    int pad_height = 0;
    int filter_width = 1;
    int filter_height = 1;
    uint8_t output_min = 0;
    uint8_t output_max = 255;

    if (argc > 7) stride = atoi(argv[5]);
    if (argc > 8) pad_width = atoi(argv[6]);
    if (argc > 9) pad_height = atoi(argv[7]);
    if (argc > 10) filter_width = atoi(argv[8]);
    if (argc > 11) filter_height = atoi(argv[9]);
    if (argc > 12) output_min = atoi(argv[10]);
    if (argc > 13) output_max = atoi(argv[11]);

    Halide::Runtime::Buffer<uint8_t> output_tensor(nullptr, C, W / stride, H / stride, N);

#ifdef HALIDE_RUNTIME_HEXAGON
    input_tensor.device_malloc(halide_hexagon_device_interface());
    output_tensor.device_malloc(halide_hexagon_device_interface());
#else
    input_tensor.allocate();
    output_tensor.allocate();
#endif

    input_tensor.for_each_value([](uint8_t &x) {
        x = static_cast<uint8_t>(rand());
    });

#ifdef HALIDE_RUNTIME_HEXAGON
    // To avoid the cost of powering HVX on in each call of the
    // pipeline, power it on once now. Also, set Hexagon performance to turbo.
    halide_hexagon_set_performance_mode(nullptr, halide_hexagon_power_turbo);
    halide_hexagon_power_hvx_on(nullptr);
#endif

    printf("Running pipeline...\n");
    double time = Halide::Tools::benchmark([&]() {
        int result = MaxPool(input_tensor, stride, pad_width, pad_height, filter_width, filter_height, output_min, output_max, output_tensor);
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
    output_tensor.copy_to_host();

    // Validate that the algorithm did what we expect.
    output_tensor.for_each_element([&](int c, int x, int y, int b) {
        int32_t output = std::numeric_limits<int32_t>::min();

        for (int iy = 0; iy < filter_height; iy++) {
            for (int ix = 0; ix < filter_width; ix++) {
                int32_t input = 0;
                int input_x = x * stride + ix - pad_width;
                int input_y = y * stride + iy - pad_height;
                if ((input_x >= 0) && (input_x < W) && (input_y >= 0) && (input_y < H)) {
                    input = static_cast<int32_t>(input_tensor(c, input_x, input_y, b));
                }
                output = std::max(output, input);
            }
        }

        output = std::max(output, (int32_t)output_min);
        output = std::min(output, (int32_t)output_max);
        if (output != output_tensor(c, x, y, b)) {
            printf("Mismatch at %d %d %d %d: %d != %d\n", c, x, y, b, output, output_tensor(c, x, y, b));
            abort();
        }
    });

    printf("Success!\n");
    return 0;
}
