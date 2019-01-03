#include <cstdio>
#include <chrono>

#include "local_laplacian.h"
#ifndef NO_AUTO_SCHEDULE
#include "local_laplacian_classic_auto_schedule.h"
#include "local_laplacian_auto_schedule.h"
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
    const int samples = atoi(argv[5]);
    const int iterations = 1;

    three_way_bench(
        [&]() { local_laplacian(input, levels, alpha/(levels-1), beta, output); },
    #ifdef NO_AUTO_SCHEDULE
        nullptr,
        nullptr,
    #else
        [&]() { local_laplacian_classic_auto_schedule(input, levels, alpha/(levels-1), beta, output); },
        [&]() { local_laplacian_auto_schedule(input, levels, alpha/(levels-1), beta, output); },
    #endif
        samples,
        iterations
    );

    convert_and_save_image(output, argv[6]);

    return 0;
}
