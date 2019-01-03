#include <cstdio>
#include <chrono>

#include "conv_layer.h"
#include "conv_layer_classic_auto_schedule.h"
#include "conv_layer_auto_schedule.h"

#include "halide_benchmark.h"
#include "HalideBuffer.h"

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

    // Timing code

    // Manually-tuned version
    double min_t_manual = benchmark(10, 10, [&]() {
        conv_layer(input, filter, bias, output);
    });
    printf("Manually-tuned time: %gms\n", min_t_manual * 1e3);

    // Auto-scheduled version
    double min_t_classic_auto = benchmark(10, 10, [&]() {
        conv_layer_classic_auto_schedule(input, filter, bias, output);
    });
    printf("Classic auto-scheduled time: %gms\n", min_t_classic_auto * 1e3);

    // Auto-scheduled version
    double min_t_auto = benchmark(10, 10, [&]() {
        conv_layer_auto_schedule(input, filter, bias, output);
    });
    printf("Auto-scheduled time: %gms\n", min_t_auto * 1e3);

    return 0;
}
