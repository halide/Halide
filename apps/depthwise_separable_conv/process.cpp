#include <cstdio>

#include "depthwise_separable_conv.h"
#include "depthwise_separable_conv_auto_schedule.h"
#include "depthwise_separable_conv_c.h"

#include "HalideBuffer.h"
#include "halide_benchmark.h"

using namespace Halide::Tools;
using namespace Halide::Runtime;

int main(int argc, char **argv) {
    // Second layer of MobileNet v2
    const int N = 4, CI = 32, CO = 16, CM = 1, W = 112, H = 112;

    Buffer<float> input(CI, W, H, N);
    Buffer<float> depthwise_filter(CM, CI, 3, 3);
    Buffer<float> pointwise_filter(CO, CI * CM);
    Buffer<float> bias(CO);

    for (int c = 0; c < input.dim(3).extent(); c++) {
        for (int z = 0; z < input.channels(); z++) {
            for (int y = 0; y < input.height(); y++) {
                for (int x = 0; x < input.width(); x++) {
                    input(x, y, z, c) = rand();
                }
            }
        }
    }

    for (int c = 0; c < depthwise_filter.dim(3).extent(); c++) {
        for (int z = 0; z < depthwise_filter.channels(); z++) {
            for (int y = 0; y < depthwise_filter.height(); y++) {
                for (int x = 0; x < depthwise_filter.width(); x++) {
                    depthwise_filter(x, y, z, c) = rand();
                }
            }
        }
    }

    for (int y = 0; y < pointwise_filter.height(); y++) {
        for (int x = 0; x < pointwise_filter.width(); x++) {
            pointwise_filter(x, y) = rand();
        }
    }

    for (int x = 0; x < bias.width(); x++) {
        bias(x) = rand();
    }

    Buffer<float> output(CO, W, H, N);
    output.fill(0.0f);

    // Manually-tuned version
    double best_manual = benchmark([&]() {
        depthwise_separable_conv(input,
                                 depthwise_filter,
                                 pointwise_filter,
                                 bias,
                                 output);
        output.device_sync();
    });
    printf("Manually-tuned time: %gms\n", best_manual * 1e3);

    // Auto-scheduled version
    double best_auto = benchmark([&]() {
        depthwise_separable_conv_auto_schedule(input,
                                               depthwise_filter,
                                               pointwise_filter,
                                               bias,
                                               output);
        output.device_sync();
    });
    printf("Auto-scheduled time: %gms\n", best_auto * 1e3);

    printf("Running generated C++ code...\n");
    Buffer<float> output_c(CO, W, H, N);
    output_c.fill(0.0f);
    depthwise_separable_conv_c(input, depthwise_filter, pointwise_filter, bias, output_c);

    int mismatch_count = 0;
    for (int c = 0; c < output_c.dim(3).extent(); c++) {
        for (int z = 0; z < output_c.channels(); z++) {
            for (int y = 0; y < output_c.height(); y++) {
                for (int x = 0; x < output_c.width(); x++) {
                    if (abs(output_c(x, y, z, c) - output_c(x, y, z, c)) > 0.00001) {
                        mismatch_count++;
                    }
                }
            }
        }
    }
    printf("Mismtach count for generated C++ code: %d\n", mismatch_count);

    printf("Success!\n");

    return 0;
}
