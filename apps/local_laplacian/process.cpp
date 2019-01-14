#include <cstdio>
#include <chrono>

#include "local_laplacian.h"
#ifndef NO_AUTO_SCHEDULE
#include "local_laplacian_classic_auto_schedule.h"
#include "local_laplacian_auto_schedule.h"
#include "local_laplacian_simple_auto_schedule.h"
#endif

#include "benchmark_util.h"
#include "HalideBuffer.h"
#include "halide_image_io.h"

using namespace Halide::Runtime;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc < 7) {
        printf("Usage: ./process input.png levels alpha beta timing_iterations output.png\n"
               "e.g.: ./process input.png 8 1 1 10 output.png\n");
        return 0;
    }

    // Input may be a PNG8
    Buffer<uint16_t> input = load_and_convert_image(argv[1]);

    int levels = atoi(argv[2]);
    float alpha = atof(argv[3]), beta = atof(argv[4]);
    Buffer<uint16_t> output(input.width(), input.height(), 3);

    multi_way_bench({
        {"Manual", [&]() { local_laplacian(input, levels, alpha/(levels-1), beta, output); output.device_sync(); }},
    #ifndef NO_AUTO_SCHEDULE
        {"Classic auto-scheduled", [&]() { local_laplacian_classic_auto_schedule(input, levels, alpha/(levels-1), beta, output); output.device_sync(); }},
        {"Auto-scheduled", [&]() { local_laplacian_auto_schedule(input, levels, alpha/(levels-1), beta, output); output.device_sync(); }},
        {"Simple auto-scheduled", [&]() { local_laplacian_simple_auto_schedule(input, levels, alpha/(levels-1), beta, output); output.device_sync(); }}
    #endif
        }
    );


    convert_and_save_image(output, argv[6]);

    return 0;
}
