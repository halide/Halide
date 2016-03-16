#include <cstdio>
#ifndef NOCHRONO
#include <chrono>
#endif

#include "local_laplacian.h"

#include "benchmark.h"
#include "stdint.h"
#include "halide_image.h"
#include "halide_image_io.h"

#if defined(__hexagon__)
#include "hexagon_standalone.h"
#include "io.h"
#endif

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc < 7) {
        printf("Usage: ./process input.png levels alpha beta timing_iterations output.png\n"
               "e.g.: ./process input.png 8 1 1 10 output.png\n");
        return 0;
    }

    Image<uint8_t> input = load_image(argv[1]);
#ifdef DEBUG
    info(input, "input");
    stats(input, "input");
#endif
    int levels = atoi(argv[2]);
    float alpha = atof(argv[3]), beta = atof(argv[4]);
    Image<uint8_t> output(input.width(), input.height(), 3);
    int timing = atoi(argv[5]);
#if defined(__hexagon__)
    SIM_ACQUIRE_HVX;
#if LOG2VLEN == 7
    SIM_SET_HVX_DOUBLE_MODE;
#endif
#endif

    // Timing code
    double best = benchmark(timing, 1, [&]() {
        local_laplacian(levels, alpha/(levels-1), beta, input, output);
    });
#ifdef PCYCLES
    best = best/output.height()/output.width()/timing;
    fprintf(stderr, "local_laplacian:\t%0.4f cycles/pixel\n", best);
#else
    printf("%gus\n", best * 1e6);
#endif

    // local_laplacian(levels, alpha/(levels-1), beta, input, output);

#if defined(__hexagon__)
    SIM_RELEASE_HVX;
#if DEBUG
    printf ("Done calling the halide func. and released the vector context\n");
#endif
#endif

    fprintf(stderr, "output: %s\n", argv[6]);
#ifndef NOSAVE
    save_image(output, argv[6]);
#endif
    fprintf(stderr, "        %d %d\n", output.width(), output.height());

    return 0;
}
