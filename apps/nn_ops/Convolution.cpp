#include <assert.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#include <cmath>
#include <limits>

#include "halide_benchmark.h"

#include "Convolution.h"
#include "common_reference.h"

#include "HalideBuffer.h"

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

    // These parameters lead to reasonable values for testing in
    // most cases (expected value of the input matrices is ~0,
    // expected value of the product is ~0).

    // Needed to define filter dimensions.
    int filter_width = 1;
    int filter_height = 1;
    int output_depth = C;

    int16_t input_offset = -128;
    int16_t filter_offset = -128;
    int input_depth = C;

    int stride = 1;
    int pad_width = 0;
    int pad_height = 0;
    uint8_t byte_zero = 0;

    int output_multiplier = 1 << 30;
    int output_shift = 8;
    int output_offset = 128;

    uint8_t output_min = 0;
    uint8_t output_max = 255;

    if (argc > 7) filter_width = atoi(argv[5]);
    if (argc > 8) filter_height = atoi(argv[6]);
    if (argc > 9) output_depth = atoi(argv[7]);
    if (argc > 10) input_offset = atoi(argv[8]);
    if (argc > 11) filter_offset = atoi(argv[9]);
    if (argc > 12) input_depth = atoi(argv[10]);
    if (argc > 13) stride = atoi(argv[11]);
    if (argc > 14) pad_width = atoi(argv[12]);
    if (argc > 15) pad_height = atoi(argv[13]);
    if (argc > 16) byte_zero = atoi(argv[14]);
    if (argc > 17) output_multiplier = atoi(argv[15]);
    if (argc > 18) output_shift = atoi(argv[16]);
    if (argc > 19) output_offset = atoi(argv[17]);
    if (argc > 20) output_min = atoi(argv[18]);
    if (argc > 21) output_max = atoi(argv[19]);

    // Hexagon's device_malloc implementation will also set the host
    // pointer if it is null, giving a zero copy buffer.
    Halide::Runtime::Buffer<uint8_t> input_tensor(nullptr, C, W, H, N);
    Halide::Runtime::Buffer<uint8_t> filter_tensor(nullptr,
                                                   input_depth, filter_width, filter_height, output_depth);
    Halide::Runtime::Buffer<int32_t> bias_tensor(nullptr, output_depth);

    const int output_width = ceil((W + 2 * pad_width - filter_width) / stride) + 1;
    const int output_height = ceil((H + 2 * pad_height - filter_height) / stride) + 1;

    Halide::Runtime::Buffer<uint8_t> output_tensor(nullptr,
                                                   output_depth, output_width, output_height, N);

#ifdef HALIDE_RUNTIME_HEXAGON
    input_tensor.device_malloc(halide_hexagon_device_interface());
    filter_tensor.device_malloc(halide_hexagon_device_interface());
    bias_tensor.device_malloc(halide_hexagon_device_interface());
    output_tensor.device_malloc(halide_hexagon_device_interface());
#else
    input_tensor.allocate();
    filter_tensor.allocate();
    bias_tensor.allocate();
    output_tensor.allocate();
#endif

    input_tensor.for_each_value([](uint8_t &x) {
        x = static_cast<uint8_t>(rand());
    });

    filter_tensor.for_each_value([](uint8_t &x) {
        x = static_cast<uint8_t>(rand());
    });

    bias_tensor.for_each_value([](int32_t &x) {
        x = static_cast<int32_t>(rand());
    });

#ifdef HALIDE_RUNTIME_HEXAGON
    // To avoid the cost of powering HVX on in each call of the
    // pipeline, power it on once now. Also, set Hexagon performance to turbo.
    halide_hexagon_set_performance_mode(nullptr, halide_hexagon_power_turbo);
    halide_hexagon_power_hvx_on(nullptr);
#endif

    printf("Running pipeline...\n");
    double time = Halide::Tools::benchmark([&]() {
        int result = Convolution(input_tensor, filter_tensor, bias_tensor,
                                 input_offset, filter_offset, input_depth,
                                 stride, pad_width, pad_height, byte_zero,
                                 output_multiplier, output_shift, output_offset,
                                 output_min, output_max, output_tensor);
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
        int32_t output = bias_tensor(c);

        for (int filter_y = 0; filter_y < filter_height; filter_y++) {
            for (int filter_x = 0; filter_x < filter_width; filter_x++) {
                for (int index_c = 0; index_c < input_depth; index_c++) {
                    int32_t input_value = static_cast<int32_t>(byte_zero);

                    int x_offset = x * stride + filter_x - pad_width;
                    int y_offset = y * stride + filter_y - pad_height;
                    if ((x_offset >= 0) && (x_offset < W) && (y_offset >= 0) && (y_offset < H)) {
                        input_value = static_cast<int32_t>(
                            (int16_t)input_tensor(index_c, x_offset, y_offset, b) + input_offset);
                    }
                    int32_t filter_value = static_cast<int32_t>(
                        (int16_t)filter_tensor(index_c, filter_x, filter_y, c) + filter_offset);

                    output += input_value * filter_value;
                }
            }
        }

        output = multiply_quantized_multiplier_reference(output, output_multiplier, output_shift);
        output += output_offset;
        output = std::max(output, (int32_t)output_min);
        output = std::min(output, (int32_t)output_max);
        if (output != output_tensor(c, x, y, b)) {
            printf("Mismatch at %d %d: %d != %d\n", x, y, output, output_tensor(c, x, y, b));
            abort();
        }
    });

    printf("Success!\n");
    return 0;
}
