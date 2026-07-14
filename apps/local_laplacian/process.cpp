#include <chrono>
#include <cstdio>

#include "local_laplacian.h"
#ifndef NO_AUTO_SCHEDULE
#include "local_laplacian_auto_schedule.h"
#endif

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
    Buffer<uint16_t, 3> input = load_and_convert_image(argv[1]);

    int levels = atoi(argv[2]);
    float alpha = atof(argv[3]), beta = atof(argv[4]);
    Buffer<uint16_t, 3> output(input.width(), input.height(), 3);
    int timing = atoi(argv[5]);

    local_laplacian(input, levels, alpha / (levels - 1), beta, output);

    // Timing code

    BenchmarkConfig config;
    config.comparison_rounds = timing;

#ifndef NO_AUTO_SCHEDULE
    auto [manual, auto_scheduled] = benchmark_comparison(
        config,
        [&]() {
            local_laplacian(input, levels, alpha / (levels - 1), beta, output);
            output.device_sync();
        },
        [&]() {
            local_laplacian_auto_schedule(input, levels, alpha / (levels - 1), beta, output);
            output.device_sync();
        });
    printf("Manually-tuned time: %gms\n", manual.wall_time * 1e3);
    printf("Auto-scheduled time: %gms\n", auto_scheduled.wall_time * 1e3);
#else
    auto manual = benchmark(
        [&]() {
            local_laplacian(input, levels, alpha / (levels - 1), beta, output);
            output.device_sync();
        },
        config);
    printf("Manually-tuned time: %gms\n", manual.wall_time * 1e3);
#endif

    convert_and_save_image(output, argv[6]);

    printf("Success!\n");
    return 0;
}
