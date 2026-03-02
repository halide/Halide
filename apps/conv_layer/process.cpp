#include <chrono>
#include <cstdio>

#include "conv_layer.h"
#include "conv_layer_auto_schedule.h"

#include "HalideBuffer.h"
#include "halide_benchmark.h"

using namespace Halide::Tools;
using namespace Halide::Runtime;

int main(int argc, char **argv) {
    const int N = 1, CI = 4, CO = 4, W = 272, H = 256;

    // Use same spatial size for input as output ("same" padding handled in generator)
    Buffer<float, 4> input(CI, W, H, N);
    Buffer<float, 4> filter(CO, 3, 3, CI);
    Buffer<float, 1> bias(CO);
    Buffer<float, 1> alpha(CO);

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

    // Initialize PReLU alpha (negative slope) per output channel
    for (int i = 0; i < alpha.width(); i++) {
        alpha(i) = 0.25f; // default negative slope
    }

    Buffer<float, 4> output(CO, W, H, N);

// This is necessary to get the PTX compiler to do a good
// job. TODO: This should be a scheduling directive or a runtime
// function.
// #ifdef _WIN32
//     _putenv_s("HL_CUDA_JIT_MAX_REGISTERS", "256");
// #else
//     setenv("HL_CUDA_JIT_MAX_REGISTERS", "256", 1);
// #endif

    conv_layer(input, filter, bias, alpha, output);

    // Timing code

    // Manually-tuned version
    double min_t_manual = benchmark(10, 10, [&]() {
        conv_layer(input, filter, bias, alpha, output);
        output.device_sync();
    });
    printf("Manually-tuned time: %gms\n", min_t_manual * 1e3);

    // Auto-scheduled version
    double min_t_auto = benchmark(10, 10, [&]() {
        conv_layer_auto_schedule(input, filter, bias, alpha, output);
        output.device_sync();
    });
    printf("Auto-scheduled time: %gms\n", min_t_auto * 1e3);

    printf("Success!\n");
    return 0;
}
