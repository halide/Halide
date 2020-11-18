#include <assert.h>
#include <cmath>
#include <limits>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#include "ConvolutionUint8.h"
#include "HalideBuffer.h"
#include "common_reference.h"
#include "halide_benchmark.h"

using interpret_nn::multiply_quantized;

namespace {

struct TestParams {
    int input_depth;
    int input_width;
    int input_height;
    int input_batches;
    int filter_width;
    int filter_height;
    int filter_batches;
    int input_offset;
    int filter_offset;
    int stride;
    int dilation;
};

const static TestParams test_params[] = {
    // mobilenet_v2_1.0_224_quant layers, duplicate shapes are commented.
    {3, 224, 224, 1, 3, 3, 32, 128, 122, 2, 1},
    {32, 112, 112, 1, 1, 1, 16, 0, 140, 1, 1},
    {16, 112, 112, 1, 1, 1, 96, 129, 127, 1, 1},
    {96, 56, 56, 1, 1, 1, 24, 0, 156, 1, 1},
    {24, 56, 56, 1, 1, 1, 144, 119, 144, 1, 1},
    {144, 56, 56, 1, 1, 1, 24, 0, 122, 1, 1},
    //{24, 56, 56, 1, 1, 1, 144, 133, 104, 1, 1},
    {144, 28, 28, 1, 1, 1, 32, 0, 111, 1, 1},
    //{32, 28, 28, 1, 1, 1, 192, 127, 128, 1, 1},
    //{192, 28, 28, 1, 1, 1, 32, 0, 146, 1, 1},
    //{32, 28, 28, 1, 1, 1, 192, 130, 135, 1, 1},
    //{192, 28, 28, 1, 1, 1, 32, 0, 128, 1, 1},
    {32, 28, 28, 1, 1, 1, 192, 124, 127, 1, 1},
    {192, 14, 14, 1, 1, 1, 64, 0, 147, 1, 1},
    {64, 14, 14, 1, 1, 1, 384, 126, 125, 1, 1},
    {384, 14, 14, 1, 1, 1, 64, 0, 124, 1, 1},
    //{64, 14, 14, 1, 1, 1, 384, 122, 134, 1, 1},
    //{384, 14, 14, 1, 1, 1, 64, 0, 125, 1, 1},
    //{64, 14, 14, 1, 1, 1, 384, 124, 127, 1, 1},
    //{384, 14, 14, 1, 1, 1, 64, 0, 144, 1, 1},
    //{64, 14, 14, 1, 1, 1, 384, 120, 131, 1, 1},
    {384, 14, 14, 1, 1, 1, 96, 0, 129, 1, 1},
    {96, 14, 14, 1, 1, 1, 576, 129, 134, 1, 1},
    {576, 14, 14, 1, 1, 1, 96, 0, 136, 1, 1},
    //{96, 14, 14, 1, 1, 1, 576, 127, 138, 1, 1},
    //{576, 14, 14, 1, 1, 1, 96, 0, 154, 1, 1},
    {96, 14, 14, 1, 1, 1, 576, 126, 123, 1, 1},
    {576, 7, 7, 1, 1, 1, 160, 0, 140, 1, 1},
    //{160, 7, 7, 1, 1, 1, 960, 132, 135, 1, 1},
    //{960, 7, 7, 1, 1, 1, 160, 0, 139, 1, 1},
    //{160, 7, 7, 1, 1, 1, 960, 134, 127, 1, 1},
    {960, 7, 7, 1, 1, 1, 160, 0, 131, 1, 1},
    {160, 7, 7, 1, 1, 1, 960, 131, 135, 1, 1},
    {960, 7, 7, 1, 1, 1, 320, 0, 111, 1, 1},
    {320, 7, 7, 1, 1, 1, 1280, 130, 125, 1, 1},
    {1280, 1, 1, 1, 1, 1, 1001, 0, 113, 1, 1},
};

struct ConvolutionArgs {
    Halide::Runtime::Buffer<uint8_t> input_tensor;
    Halide::Runtime::Buffer<uint8_t> filter_tensor;
    Halide::Runtime::Buffer<int32_t> bias_tensor;
    // TODO: Just use TestParams here?
    int input_offset;
    int filter_offset;
    int stride_x;
    int stride_y;
    int dilation_x;
    int dilation_y;
    int output_multiplier;
    int output_shift;
    int output_offset;
    int output_min;
    int output_max;
    Halide::Runtime::Buffer<uint8_t> output_tensor;

    explicit ConvolutionArgs(const TestParams &p) {
        // These parameters lead to reasonable values for testing in
        // most cases (expected value of the input matrices is ~0,
        // expected value of the product is ~0).
        const int filter_depth = p.input_depth;
        const int output_depth = p.filter_batches;
        const int output_width = ceil((p.input_width - p.filter_width) / p.stride) + 1;
        const int output_height = ceil((p.input_height - p.filter_height) / p.stride) + 1;
        const int output_batches = p.input_batches;

        input_tensor = Halide::Runtime::Buffer<uint8_t>(p.input_depth, p.input_width, p.input_height, p.input_batches);
        filter_tensor = Halide::Runtime::Buffer<uint8_t>(filter_depth, p.filter_width, p.filter_height, p.filter_batches);
        bias_tensor = Halide::Runtime::Buffer<int32_t>(p.filter_batches);

        output_tensor = Halide::Runtime::Buffer<uint8_t>(output_depth, output_width, output_height, output_batches);

        input_tensor.for_each_value([](uint8_t &x) {
            x = static_cast<uint8_t>(rand());
        });

        filter_tensor.for_each_value([](uint8_t &x) {
            x = static_cast<uint8_t>(rand());
        });

        bias_tensor.for_each_value([](int32_t &x) {
            // Bias values are 32-bit, but values that are too large can lead
            // to signed over-/underflow, which is undefined behavior in
            // C/C++ and Halide. We avoid that by restricting the magnitude of
            // the values here.
            x = static_cast<int16_t>(rand());
        });

        input_offset = p.input_offset;
        filter_offset = p.filter_offset;
        stride_x = p.stride;
        stride_y = p.stride;
        dilation_x = p.dilation;
        dilation_y = p.dilation;
        output_multiplier = 1 << 20;
        output_shift = 0;
        output_offset = 0;
        output_min = 0;
        output_max = 255;
    }
};

void RunBenchmark(ConvolutionArgs &a) {
    double time = Halide::Tools::benchmark([&]() {
        int result = interpret_nn::ConvolutionUint8(a.input_tensor, a.filter_tensor, a.bias_tensor,
                                                    a.input_offset, a.filter_offset, a.stride_x,
                                                    a.stride_y, a.dilation_x, a.dilation_y,
                                                    a.output_multiplier, a.output_shift, a.output_offset,
                                                    a.output_min, a.output_max, a.output_tensor);
        if (result != 0) {
            fprintf(stderr, "pipeline failed! %d\n", result);
        }
    });

    printf("Done, time: %g s\n", time);
}

void ValidateOutput(ConvolutionArgs &a, const TestParams &p) {
    // Validate that the algorithm did what we expect.
    a.output_tensor.for_each_element([&](int c, int x, int y, int b) {
        int32_t output = a.bias_tensor(c);

        for (int filter_y = 0; filter_y < p.filter_height; filter_y++) {
            for (int filter_x = 0; filter_x < p.filter_width; filter_x++) {
                for (int index_c = 0; index_c < p.input_depth; index_c++) {
                    int32_t input_value = 0;
                    int x_offset = x * p.stride + filter_x * p.dilation;
                    int y_offset = y * p.stride + filter_y * p.dilation;
                    if ((x_offset >= 0) && (x_offset < p.input_width) && (y_offset >= 0) && (y_offset < p.input_height)) {
                        input_value =
                            (int16_t)a.input_tensor(index_c, x_offset, y_offset, b) - (int16_t)p.input_offset;
                    }
                    int32_t filter_value =
                        (int16_t)a.filter_tensor(index_c, filter_x, filter_y, c) - (int16_t)p.filter_offset;

                    output += input_value * filter_value;
                }
            }
        }

        output = multiply_quantized(output, a.output_multiplier, a.output_shift);
        output += a.output_offset;
        output = std::max(output, (int32_t)a.output_min);
        output = std::min(output, (int32_t)a.output_max);
        if (output != a.output_tensor(c, x, y, b)) {
            fprintf(stderr, "Mismatch at %d %d: %d != %d\n", x, y, output, a.output_tensor(c, x, y, b));
            abort();
        }
    });
}

}  // namespace

int main(int argc, char **argv) {
    for (const auto &p : test_params) {
        printf("Benchmarking %dx%dx%dx%d\n", p.input_depth, p.input_width, p.input_height, p.input_batches);

        ConvolutionArgs a(p);
        RunBenchmark(a);

        halide_profiler_report(nullptr);
        halide_profiler_reset();

        ValidateOutput(a, p);
    }

    printf("Success!\n");
    return 0;
}
