#include <cstdio>
#include <chrono>

#include "local_laplacian.h"

#include "benchmark.h"
#include "HalideBuffer.h"
#include "halide_image_io.h"

using namespace Halide;

int main(int argc, char **argv) {
    if (argc < 7) {
        printf("Usage: ./process input.png levels alpha beta timing_iterations output.png\n"
               "e.g.: ./process input.png 8 1 1 10 output.png\n");
        return 0;
    }

    Image<uint16_t> input = Tools::load_image(argv[1]);
    int levels = atoi(argv[2]);
    float alpha = atof(argv[3]), beta = atof(argv[4]);
    Image<uint16_t> output(input.width(), input.height(), 3);
    int timing = atoi(argv[5]);

    // Timing code
    double best = benchmark(timing, 1, [&]() {
        local_laplacian(levels, alpha/(levels-1), beta, input, output);
    });
    printf("%gus\n", best * 1e6);


    local_laplacian(levels, alpha/(levels-1), beta, input, output);

    Tools::save_image(output, argv[6]);

    return 0;
}
