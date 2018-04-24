#include <assert.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#include <cmath>
#include <limits>

#include "halide_benchmark.h"

#include "common_reference.h"
#include "DepthwiseConvolution_1.h"
#include "DepthwiseConvolution_2.h"
#include "DepthwiseConvolution_4.h"
#include "DepthwiseConvolution_8.h"

#include "HalideBuffer.h"

int main(int argc, char **argv) {
    if (argc < 5) {
        printf("Usage: %s C W H N [filter_width, filter_height, input_offset, filter_offset, output_multiplier, output_shift, output_offset, stride, pad_width, pad_height, output_min, output_max]\n", argv[0]);
        return 0;
    }

    int C = atoi(argv[1]);
    int W = atoi(argv[2]);
    int H = atoi(argv[3]);
    int N = atoi(argv[4]);

    printf("Benchmarking %dx%dx%dx%d\n", C, W, H, N);

    // TODO: We could reduce k_alignment on some targets. 128 is
    // conservative to enable Hexagon with 128-byte vectors.
    int c_alignment = 128;

    // Align the dimensions as required.
    C = (C + c_alignment - 1) & ~(c_alignment - 1);

    // These parameters lead to reasonable values for testing in
    // most cases (expected value of the input matrices is ~0,
    // expected value of the product is ~0).

    // Needed to define filter dimensions.
    int filter_width = 1;
    int filter_height = 1;

    int depth_multiplier = 1;

    int16_t input_offset = -128;
    int16_t filter_offset = -128;

    int output_multiplier = 1 << 30;
    int output_shift = 8;
    int output_offset = 128;

    int stride = 1;
    int pad_width = 0;
    int pad_height = 0;

    uint8_t output_min = 0;
    uint8_t output_max = 255;

    if (argc > 7) filter_width = atoi(argv[5]);
    if (argc > 8) filter_height = atoi(argv[6]);
    if (argc > 9) depth_multiplier = atoi(argv[7]);
    if (argc > 10) input_offset = atoi(argv[8]);
    if (argc > 11) filter_offset = atoi(argv[9]);
    if (argc > 12) output_multiplier = atoi(argv[10]);
    if (argc > 13) output_shift = atoi(argv[11]);
    if (argc > 14) output_offset = atoi(argv[12]);
    if (argc > 15) stride = atoi(argv[13]);
    if (argc > 16) pad_width = atoi(argv[14]);
    if (argc > 17) pad_height = atoi(argv[15]);
    if (argc > 18) output_min = atoi(argv[16]);
    if (argc > 19) output_max = atoi(argv[17]);

    // Hexagon's device_malloc implementation will also set the host
    // pointer if it is null, giving a zero copy buffer.
    Halide::Runtime::Buffer<uint8_t> input_tensor(nullptr, C, W, H, N);
    Halide::Runtime::Buffer<uint8_t> filter_tensor(nullptr, depth_multiplier * C, filter_width, filter_height);
    Halide::Runtime::Buffer<int32_t> bias_tensor(nullptr, depth_multiplier * C);

    Halide::Runtime::Buffer<uint8_t> output_tensor(nullptr, depth_multiplier * C, W / stride, H / stride, N);

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

    auto dw_convolution_fn = DepthwiseConvolution_1;
    if (depth_multiplier == 1) {
        dw_convolution_fn = DepthwiseConvolution_1;
    } else if (depth_multiplier == 2) {
        dw_convolution_fn = DepthwiseConvolution_2;
    } else if (depth_multiplier == 4) {
        dw_convolution_fn = DepthwiseConvolution_4;
    } else if (depth_multiplier == 8) {
        dw_convolution_fn = DepthwiseConvolution_8;
    } else {
        printf("This depth multiplier is not covered by this test\n");
        abort();
    }

    printf("Running pipeline...\n");
    double time = Halide::Tools::benchmark([&]() {
        int result = dw_convolution_fn(input_tensor, filter_tensor, bias_tensor,
                                       input_offset, filter_offset,
                                       output_multiplier, output_shift, output_offset,
                                       stride, pad_width, pad_height,
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
                int32_t input_value = -input_offset;

                int x_offset = x * stride + filter_x - pad_width;
                int y_offset = y * stride + filter_y - pad_height;
                if ((x_offset >= 0) && (x_offset < W) && (y_offset >= 0) && (y_offset < H)) {
                    input_value = static_cast<int32_t>(
                        (int16_t) input_tensor(c / depth_multiplier, x_offset, y_offset, b) + input_offset);
                }
                int32_t filter_value = static_cast<int32_t>(
                    (int16_t) filter_tensor(c, filter_x, filter_y) + filter_offset);

                output += input_value * filter_value;
            }
        }

        output = multiply_quantized_multiplier_reference(output, output_multiplier, output_shift);
        output += output_offset;
        output = std::max(output, (int32_t) output_min);
        output = std::min(output, (int32_t) output_max);
        if (output != output_tensor(c, x, y, b)) {
            printf("Mismatch at %d %d: %d != %d\n", x, y, output, output_tensor(c, x, y, b));
            abort();
        }
    });

    printf("Success!\n");
    return 0;
}
