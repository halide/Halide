#include <chrono>
#include <cstdio>

#include "depthwise_separable_conv.h"
#ifndef NO_AUTO_SCHEDULE
    #include "depthwise_separable_conv_auto_schedule.h"
#endif
#ifndef NO_GRADIENT_AUTO_SCHEDULE
    #include "depthwise_separable_conv_gradient_auto_schedule.h"
#endif

#include "benchmark_util.h"
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

    depthwise_separable_conv(input,
                             depthwise_filter,
                             pointwise_filter,
                             bias,
                             1, // pad_width
                             1, // pad_height
                             output);

    // Timing code

    multi_way_bench({
        {"depthwise_separable_conv Manual", [&]() { depthwise_separable_conv(input, depthwise_filter, pointwise_filter, bias, 1, 1, output); output.device_sync(); }},
    #ifndef NO_AUTO_SCHEDULE
        {"depthwise_separable_conv Auto-schedule", [&]() { depthwise_separable_conv_auto_schedule(input, depthwise_filter, pointwise_filter, bias, 1, 1, output); output.device_sync(); }},
    #endif
    #ifndef NO_GRADIENT_AUTO_SCHEDULE
        {"depthwise_separable_conv Gradient auto-schedule", [&]() { depthwise_separable_conv_gradient_auto_schedule(input, depthwise_filter, pointwise_filter, bias, 1, 1, output); output.device_sync(); }}
    #endif
    });

    return 0;
}
