#include <cstdio>
#include <cstdlib>
#include <cassert>

#include "bilateral_grid.h"

#include "halide_benchmark.h"
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

    float r_sigma = atof(argv[3]);
    int timing_iterations = atoi(argv[4]);

    Buffer<float> input = load_image(argv[1]);
    Buffer<float> output(input.width(), input.height(), 1);

    bilateral_grid(input, r_sigma, output);

    // Timing code. Timing doesn't include copying the input data to
    // the gpu or copying the output back.
    double min_t = benchmark(timing_iterations, 10, [&]() {
        bilateral_grid(input, r_sigma, output);
    });
    printf("Time: %gms\n", min_t * 1e3);

    save_image(output, argv[2]);

    return 0;
}
