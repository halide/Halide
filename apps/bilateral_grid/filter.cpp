#include <cstdio>
#include <cstdlib>
#include <cassert>

#include "bilateral_grid.h"

#include "benchmark.h"
#include "halide_image_io.h"
#include "static_image.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {

    if (argc < 5) {
        printf("Usage: ./filter input.png output.png range_sigma timing_iterations\n"
               "e.g. ./filter input.png output.png 0.1 10\n");
        return 0;
    }

    int timing_iterations = atoi(argv[4]);

    Image<float> input = load<Image<float>>(argv[1]);
    Image<float> output(input.width(), input.height(), 1);

    bilateral_grid(atof(argv[3]), input, output);

    // Timing code
    double min_t = benchmark(timing_iterations, 10, [&]() {
        bilateral_grid(atof(argv[3]), input, output);
    });
    printf("Time: %gms\n", min_t * 1e3);

    save(output, argv[2]);

    return 0;
}
