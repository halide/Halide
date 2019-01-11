#include <cstdio>
#include <cstdlib>
#include <cassert>

#include "bilateral_grid.h"
#ifndef NO_AUTO_SCHEDULE
#include "bilateral_grid_classic_auto_schedule.h"
#include "bilateral_grid_auto_schedule.h"
#endif

#include "benchmark_util.h"
#include "HalideBuffer.h"
#include "halide_image_io.h"

using namespace Halide::Tools;
using namespace Halide::Runtime;

int main(int argc, char **argv) {
    if (argc < 5) {
        printf("Usage: ./filter input.png output.png range_sigma timing_iterations\n"
               "e.g. ./filter input.png output.png 0.1 10\n");
        return 0;
    }

    float r_sigma = (float) atof(argv[3]);

    Buffer<float> input = load_and_convert_image(argv[1]);
    Buffer<float> output(input.width(), input.height());

    three_way_bench(
        [&]() { bilateral_grid(input, r_sigma, output); output.device_sync(); },
    #ifdef NO_AUTO_SCHEDULE
        nullptr,
        nullptr
    #else
        [&]() { bilateral_grid_classic_auto_schedule(input, r_sigma, output); output.device_sync(); },
        [&]() { bilateral_grid_auto_schedule(input, r_sigma, output); output.device_sync(); }
    #endif
    );

    convert_and_save_image(output, argv[2]);

    return 0;
}
