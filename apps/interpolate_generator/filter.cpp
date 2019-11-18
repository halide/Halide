#include <cstdio>
#include <cstdlib>
#include <cassert>

#include "HalideRuntime.h"
#include "HalideBuffer.h"

#include "interpolate.h"
#include "interpolate_auto_schedule.h"
#include "interpolate_gradient_auto_schedule.h"

#include "benchmark_util.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s in out\n", argv[0]);
        return 1;
    }

    Halide::Runtime::Buffer<float> input = load_and_convert_image(argv[1]);
    assert(input.channels() == 4);

    Halide::Runtime::Buffer<float> output(input.width(), input.height(), 3);

    multi_way_bench({
        {"Manual", [&]() { interpolate(input, output); output.device_sync(); }},
        {"Auto-scheduled", [&]() { interpolate_auto_schedule(input, output); output.device_sync(); }},
        {"Gradient auto-scheduled", [&]() { interpolate_auto_schedule(input, output); output.device_sync(); }}
        });

    convert_and_save_image(output, argv[2]);

    return 0;
}
