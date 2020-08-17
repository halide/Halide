#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "harris.h"
#ifndef NO_AUTO_SCHEDULE
    #include "harris_auto_schedule.h"
#endif
#ifndef NO_GRADIENT_AUTO_SCHEDULE
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

    multi_way_bench({{"harris Manual", [&]() {
                          harris(padded_input, output);
                          output.device_sync();
                      }},
#ifndef NO_AUTO_SCHEDULE
                     {"harris Auto-scheduled", [&]() {
                          harris_auto_schedule(padded_input, output);
                          output.device_sync();
                      }},
#endif
#ifndef NO_GRADIENT_AUTO_SCHEDULE
                     {"harris Gradient auto-scheduled", [&]() {
                          harris_gradient_auto_schedule(padded_input, output);
                          output.device_sync();
                      }}
#endif
    });

    output.copy_to_host();

    convert_and_save_image(output, argv[2]);

    printf("Success!\n");
    return 0;
}
