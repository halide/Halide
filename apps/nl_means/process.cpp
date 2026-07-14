#include <chrono>
#include <cstdio>

#include "nl_means.h"
#include "nl_means_auto_schedule.h"

#include "HalideBuffer.h"
#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Runtime;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc < 7) {
        printf("Usage: ./process input.png patch_size search_area sigma timing_iterations output.png\n"
               "e.g.: ./process input.png 7 7 0.12 10 output.png\n");
        return 0;
    }

    Buffer<float, 3> input = load_and_convert_image(argv[1]);
    int patch_size = atoi(argv[2]);
    int search_area = atoi(argv[3]);
    float sigma = atof(argv[4]);
    Buffer<float, 3> output(input.width(), input.height(), 3);
    int timing_iterations = atoi(argv[5]);

    nl_means(input, patch_size, search_area, sigma, output);

    // Timing code

    printf("Input size: %d by %d, patch size: %d, search area: %d, sigma: %f\n",
           input.width(), input.height(), patch_size, search_area, sigma);

    BenchmarkConfig config;
    config.comparison_rounds = timing_iterations;

    auto [manual, auto_scheduled] = benchmark_comparison(
        config,
        [&]() {
            nl_means(input, patch_size, search_area, sigma, output);
            output.device_sync();
        },
        [&]() {
            nl_means_auto_schedule(input, patch_size, search_area, sigma, output);
            output.device_sync();
        });
    printf("Manually-tuned time: %gms\n", manual.wall_time * 1e3);
    printf("Auto-scheduled time: %gms\n", auto_scheduled.wall_time * 1e3);

    convert_and_save_image(output, argv[6]);

    printf("Success!\n");
    return 0;
}
