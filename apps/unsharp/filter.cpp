#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "unsharp.h"
#include "unsharp_auto_schedule.h"
#include "unsharp_c.h"

#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 4) {
        printf("Usage: %s in out out_c\n", argv[0]);
        return 1;
    }

    Halide::Runtime::Buffer<float, 3> input = load_and_convert_image(argv[1]);
    Halide::Runtime::Buffer<float, 3> output(input.width(), input.height(), 3);

    double best_manual = benchmark([&]() {
        unsharp(input, output);
        output.device_sync();
    });
    printf("Manually-tuned time: %gms\n", best_manual * 1e3);

    double best_auto = benchmark([&]() {
        unsharp_auto_schedule(input, output);
        output.device_sync();
    });
    printf("Auto-scheduled time: %gms\n", best_auto * 1e3);

    convert_and_save_image(output, argv[2]);

    printf("Running generated C++ code...\n");
    Halide::Runtime::Buffer<float> output_c(input.width(), input.height(), 3);
    unsharp_c(input, output_c);
    convert_and_save_image(output, argv[3]);

    printf("Success!\n");
    return 0;
}
