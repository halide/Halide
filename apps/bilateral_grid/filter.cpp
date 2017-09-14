#include <cstdio>
#include <cstdlib>
#include <cassert>

#include "bilateral_grid.h"
#include "bilateral_grid_auto_schedule.h"

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

    float r_sigma = (float) atof(argv[3]);
    int timing_iterations = atoi(argv[4]);

    Buffer<float> input = load_and_convert_image(argv[1]);
    Buffer<float> output(input.width(), input.height(), 1);

    bilateral_grid(input, r_sigma, output);

    // Timing code. Timing doesn't include copying the input data to
    // the gpu or copying the output back.

    // Manually-tuned version
    double min_t_manual = benchmark(timing_iterations, 10, [&]() {
        bilateral_grid(input, r_sigma, output);
    });
    printf("Manually-tuned time: %gms\n", min_t_manual * 1e3);

    // Auto-scheduled version
    double min_t_auto = benchmark(timing_iterations, 10, [&]() {
        bilateral_grid_auto_schedule(input, r_sigma, output);
    });
    printf("Auto-scheduled time: %gms\n", min_t_auto * 1e3);

    convert_and_save_image(output, argv[2]);

    return 0;
}
