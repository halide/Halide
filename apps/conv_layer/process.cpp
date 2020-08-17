#include <chrono>
#include <cstdio>

#include "conv_layer.h"
#ifndef NO_AUTO_SCHEDULE
    #include "conv_layer_auto_schedule.h"
#endif
#ifndef NO_GRADIENT_AUTO_SCHEDULE
    #include "conv_layer_gradient_auto_schedule.h"
#endif

#include "benchmark_util.h"
#include "HalideBuffer.h"
#include "halide_benchmark.h"

using namespace Halide::Tools;
using namespace Halide::Runtime;

int main(int argc, char **argv) {
    const int N = 5, CI = 128, CO = 128, W = 100, H = 80;

    Buffer<float> input(CI, W + 2, H + 2, N);
    Buffer<float> filter(CO, 3, 3, CI);
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

    for (int c = 0; c < filter.dim(3).extent(); c++) {
        for (int z = 0; z < filter.channels(); z++) {
            for (int y = 0; y < filter.height(); y++) {
                for (int x = 0; x < filter.width(); x++) {
                    filter(x, y, z, c) = rand();
                }
            }
        }
    }

    for (int x = 0; x < bias.width(); x++) {
        bias(x) = rand();
    }

    Buffer<float> output(CO, W, H, N);

// This is necessary to get the PTX compiler to do a good
// job. TODO: This should be a scheduling directive or a runtime
// function.
#ifdef _WIN32
    _putenv_s("HL_CUDA_JIT_MAX_REGISTERS", "256");
#else
    setenv("HL_CUDA_JIT_MAX_REGISTERS", "256", 1);
#endif

    conv_layer(input, filter, bias, output);

    // Timing code

    multi_way_bench({
        {"conv_layer Manual", [&]() { conv_layer(input, filter, bias, output); output.device_sync(); }},
#ifndef NO_AUTO_SCHEDULE
        {"conv_layer Auto-schedule", [&]() { conv_layer_auto_schedule(input, filter, bias, output); output.device_sync(); }},
#endif
#ifndef NO_GRADIENT_AUTO_SCHEDULE
        {"conv_layer Gradient auto-schedule", [&]() { conv_layer_gradient_auto_schedule(input, filter, bias, output); output.device_sync(); }}
#endif
    });

    printf("Success!\n");
    return 0;
}
