#include <cstdio>
#include <cstdlib>
#include <cassert>

#include "HalideRuntime.h"
#include "HalideBuffer.h"

#include "hist.h"
#include "hist_classic_auto_schedule.h"
#include "hist_auto_schedule.h"
#include "hist_simple_auto_schedule.h"

#include "benchmark_util.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s in out\n", argv[0]);
        return 1;
    }

    Halide::Runtime::Buffer<uint8_t> input = load_and_convert_image(argv[1]);
    Halide::Runtime::Buffer<uint8_t> output(input.width(), input.height(), 3);

    multi_way_bench({
        {"Manual", [&]() { hist(input, output); output.device_sync(); }},
        {"Classic auto-scheduled", [&]() { hist_classic_auto_schedule(input, output); output.device_sync(); }},
        {"Auto-scheduled", [&]() { hist_auto_schedule(input, output); output.device_sync(); }},
        {"Simple auto-scheduled", [&]() { hist_simple_auto_schedule(input, output); output.device_sync(); }}
    });

    convert_and_save_image(output, argv[2]);

    return 0;
}
