#include <cstdio>

#include "depthwise_separable_conv.h"
#include "depthwise_separable_conv_auto_schedule.h"

#include "HalideBuffer.h"
#include "halide_benchmark.h"

using namespace Halide::Tools;
using namespace Halide::Runtime;

int main(int argc, char **argv) {
    // Second layer of MobileNet v2
    const int N = 4, CI = 32, CO = 16, CM = 1, W = 112, H = 112;

    Buffer<float, 4> input(CI, W, H, N);
    Buffer<float, 4> depthwise_filter(CM, CI, 3, 3);
    Buffer<float, 2> pointwise_filter(CO, CI * CM);
    Buffer<float, 1> bias(CO);

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

    Buffer<float, 4> output(CO, W, H, N);
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

    printf("Success!\n");

    return 0;
}
