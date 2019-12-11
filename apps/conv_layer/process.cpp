#include <chrono>
#include <cstdio>

#include "conv_layer.h"
#include "conv_layer_auto_schedule.h"

#include "HalideBuffer.h"
#include "halide_benchmark.h"

using namespace Halide::Tools;
using namespace Halide::Runtime;

int main(int argc, char **argv) {
    Buffer<float> input(67, 67, 32, 4);
    Buffer<float> filter(3, 3, 32, 32);
    Buffer<float> bias(32);

    for (int c = 0; c < input.dim(3).extent(); c++) {
        for (int z = 0; z < input.channels(); z++) {
            for (int y = 0; y < input.height(); y++) {
                for (int x = 0; x < input.width(); x++) {
                    input(x, y) = rand();
                }
            }
        }
    }

    for (int c = 0; c < filter.dim(3).extent(); c++) {
        for (int z = 0; z < filter.channels(); z++) {
            for (int y = 0; y < filter.height(); y++) {
                for (int x = 0; x < filter.width(); x++) {
                    filter(x, y) = rand();
                }
            }
        }
    }

    for (int x = 0; x < bias.width(); x++) {
        bias(x) = rand();
    }

    Buffer<float> output(64, 64, 32, 4);

    conv_layer(input, filter, bias, output);

    // Timing code

    // Manually-tuned version
    double min_t_manual = benchmark(10, 10, [&]() {
        conv_layer(input, filter, bias, output);
        output.device_sync();
    });
    printf("Manually-tuned time: %gms\n", min_t_manual * 1e3);

    // Auto-scheduled version
    double min_t_auto = benchmark(10, 10, [&]() {
        conv_layer_auto_schedule(input, filter, bias, output);
        output.device_sync();
    });
    printf("Auto-scheduled time: %gms\n", min_t_auto * 1e3);

    return 0;
}
