#include <cstdio>
#include <cstdlib>
#include <cassert>

#include "HalideRuntime.h"
#include "HalideBuffer.h"

#include "unsharp.h"
#include "unsharp_classic_auto_schedule.h"
#include "unsharp_auto_schedule.h"

#include "benchmark_util.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s in out\n", argv[0]);
        return 1;
    }

    Halide::Runtime::Buffer<float> input = load_and_convert_image(argv[1]);
    Halide::Runtime::Buffer<float> output(input.width(), input.height(), 3);

    three_way_bench(
        [&]() { unsharp(input, output); output.device_sync(); },
        [&]() { unsharp_classic_auto_schedule(input, output); output.device_sync(); },
        [&]() { unsharp_auto_schedule(input, output); output.device_sync(); }
    );

    convert_and_save_image(output, argv[2]);

    return 0;
}
