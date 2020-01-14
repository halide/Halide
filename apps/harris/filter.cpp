#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "harris.h"
#ifndef NO_AUTO_SCHEDULE
#include "harris_auto_schedule.h"
#include "harris_gradient_auto_schedule.h"
#endif

#include "benchmark_util.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s in out\n", argv[0]);
        return 1;
    }

    Halide::Runtime::Buffer<float> input = load_and_convert_image(argv[1]);

    // The harris app doesn't use a boundary condition, so we just pad out the input instead.
    Halide::Runtime::Buffer<float> padded_input(input.width() + 64, input.height() + 64, 3);
    padded_input.set_min(-32, -32, 0);
    padded_input.copy_from(input);

    Halide::Runtime::Buffer<float> output(input.width(), input.height());

    multi_way_bench({{"Manual", [&]() {
                          harris(padded_input, output);
                          output.device_sync();
                      }},
#ifndef NO_AUTO_SCHEDULE
                     {"Auto-scheduled", [&]() {
                          harris_auto_schedule(padded_input, output);
                          output.device_sync();
                      }},
                     {"Gradient auto-scheduled", [&]() {
                          harris_gradient_auto_schedule(padded_input, output);
                          output.device_sync();
                      }}
#endif
    });

    convert_and_save_image(output, argv[2]);

    return 0;
}
