#include <chrono>
#include <cstdio>

#include "local_laplacian.h"
#ifndef NO_AUTO_SCHEDULE
    #include "local_laplacian_auto_schedule.h"
#endif
#ifndef NO_GRADIENT_AUTO_SCHEDULE
    #include "local_laplacian_gradient_auto_schedule.h"
#endif

#include "benchmark_util.h"
#include "HalideBuffer.h"
#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Runtime;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc < 7) {
        printf("Usage: ./process input.png levels alpha beta timing_iterations output.png\n"
               "e.g.: ./process input.png 8 1 1 10 output.png\n");
        return 1;
    }

    // Input may be a PNG8
    Buffer<uint16_t> input = load_and_convert_image(argv[1]);

    int levels = atoi(argv[2]);
    float alpha = atof(argv[3]), beta = atof(argv[4]);
    Buffer<uint16_t> output(input.width(), input.height(), 3);

    multi_way_bench({
        {"local_laplacian Manual", [&]() { local_laplacian(input, levels, alpha/(levels-1), beta, output); output.device_sync(); }},
    #ifndef NO_AUTO_SCHEDULE
        {"local_laplacian Auto-scheduled", [&]() { local_laplacian_auto_schedule(input, levels, alpha/(levels-1), beta, output); output.device_sync(); }},
    #endif
    #ifndef NO_GRADIENT_AUTO_SCHEDULE
        {"local_laplacian Gradient auto-scheduled", [&]() { local_laplacian_gradient_auto_schedule(input, levels, alpha/(levels-1), beta, output); output.device_sync(); }}
    #endif
        }
    );

    convert_and_save_image(output, argv[6]);

    printf("Success!\n");
    return 0;
}
