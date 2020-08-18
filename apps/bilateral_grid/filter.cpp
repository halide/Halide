#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "bilateral_grid.h"
#ifndef NO_AUTO_SCHEDULE
    #include "bilateral_grid_auto_schedule_sioutas.h"
    #include "bilateral_grid_auto_schedule_sioutas_folded.h"
#endif
#ifndef NO_GRADIENT_AUTO_SCHEDULE
    #include "bilateral_grid_gradient_auto_schedule.h"
#endif

#include "HalideBuffer.h"
#include "benchmark_util.h"
#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Tools;
using namespace Halide::Runtime;

int main(int argc, char **argv) {
    if (argc < 5) {
        printf("Usage: ./filter input.png output.png range_sigma timing_iterations\n"
               "e.g. ./filter input.png output.png 0.1 10\n");
        return 0;
    }

    float r_sigma = (float)atof(argv[3]);

    Buffer<float> input = load_and_convert_image(argv[1]);
    Buffer<float> output(input.width(), input.height());

    multi_way_bench({{"bilateral_grid Manual", [&]() { bilateral_grid(input, r_sigma, output); output.device_sync(); }},
#ifndef NO_AUTO_SCHEDULE
                     {"bilateral_grid Sioutas Auto-scheduled", [&]() { bilateral_grid_auto_schedule_sioutas(input, r_sigma, output); output.device_sync(); }},
                     {"bilateral_grid Sioutas Auto-scheduled Folded", [&]() { bilateral_grid_auto_schedule_sioutas_folded(input, r_sigma, output); output.device_sync(); }},
#endif
#ifndef NO_GRADIENT_AUTO_SCHEDULE
                     {"bilateral_grid Gradient auto-scheduled", [&]() { bilateral_grid_gradient_auto_schedule(input, r_sigma, output); output.device_sync(); }}
#endif
    });

    output.copy_to_host();

    convert_and_save_image(output, argv[2]);

    printf("Success!\n");
    return 0;
}
