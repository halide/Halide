#include <assert.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#include <limits>

#include "halide_benchmark.h"

#include "Im2col.h"

#include "HalideBuffer.h"

int clamp(int x, int low, int high) {
    return std::min(std::max(x, low), high);
}

int main(int argc, char **argv) {
    if (argc < 5) {
        printf("Usage: %s C W H N [stride pad_width pad_height filter_width filter_height byte_zero]\n", argv[0]);
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
    // most cases.
    int stride = 1;
    int pad_width = 0;
    int pad_height = 0;
    int filter_width = 1;
    int filter_height = 1;
    uint8_t byte_zero = 0;

    if (argc > 7) stride = atoi(argv[5]);
    if (argc > 8) pad_width = atoi(argv[6]);
    if (argc > 9) pad_height = atoi(argv[7]);
    if (argc > 10) filter_width = atoi(argv[8]);
    if (argc > 11) filter_height = atoi(argv[9]);
    if (argc > 12) byte_zero = atoi(argv[10]);

    const int output_depth = C * W * H;
    // Output size should be:
    // ceil((input_size + 2 * padding - filter_size) / stride) + 1
    const int output_width =
        (W + 2 * pad_width - filter_width + stride - 1) / stride + 1;
    const int output_height =
        (H + 2 * pad_height - filter_height + stride - 1) / stride + 1;

    Halide::Runtime::Buffer<uint8_t> output_tensor(nullptr,
                                                   output_depth, output_width, output_height, N);

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
        int result = Im2col(input_tensor, stride, pad_width, pad_height,
                            filter_width, filter_height, byte_zero, output_tensor);
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
        int x_ungated_start = x * stride - pad_width;
        int y_ungated_start = y * stride - pad_height;
        int element_location = c / C;
        int x_offset = element_location % filter_width;
        int y_offset = element_location / filter_width;

        int x_input = x_ungated_start + x_offset;
        int y_input = y_ungated_start + y_offset;

        int32_t output = byte_zero;

        if ((x_input >= 0) && (x_input < W) && (y_input >= 0) && (y_input < H)) {
            output = static_cast<int32_t>(
                input_tensor(c % C, x_input, y_input, b));
        }

        if (output != output_tensor(c, x, y, b)) {
            printf("Mismatch at %d %d %d %d: %d != %d\n",
                   c, x, y, b, output, output_tensor(c, x, y, b));
            abort();
        }
    });

    printf("Success!\n");
    return 0;
}
