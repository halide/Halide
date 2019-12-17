#include <chrono>
#include <cstdio>

#include "conv_layer.h"
#include "conv_layer_auto_schedule.h"
#include "conv_layer_gradient_auto_schedule.h"

#include "benchmark_util.h"
#include "HalideBuffer.h"
#include "halide_benchmark.h"

using namespace Halide::Tools;
using namespace Halide::Runtime;

int main(int argc, char **argv) {

    const int N = 5, CI = 120, CO = 24, W = 100, H = 80;

    Buffer<float> input(CI, W+2, H+2, N);
    Buffer<float> filter(CO, 3, 3, CI);
    Buffer<float> bias(CO);

    input.for_each_value([&](float &f) {f = (double)rand()/RAND_MAX;});
    filter.for_each_value([&](float &f) {f = (double)rand()/RAND_MAX;});
    bias.for_each_value([&](float &f) {f = (double)rand()/RAND_MAX;});

    Buffer<float> output(CO, W, H, N);

    multi_way_bench({
        {"Manual", [&]() { conv_layer(input, filter, bias, output); output.device_sync(); }},
        {"Auto-schedule", [&]() { conv_layer_auto_schedule(input, filter, bias, output); output.device_sync(); }},
        {"Gradient auto-schedule", [&]() { conv_layer_gradient_auto_schedule(input, filter, bias, output); output.device_sync(); }}
    });

    return 0;
}
