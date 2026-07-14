#include <chrono>
#include <cstdio>

#include "stencil_chain.h"
#ifndef NO_AUTO_SCHEDULE
#include "stencil_chain_auto_schedule.h"
#endif

#include "HalideBuffer.h"
#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Runtime;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: ./process input.png timing_iterations output.png\n"
               "e.g.: ./process input.png 10 output.png\n");
        return 0;
    }

    // Input may be a PNG8
    Buffer<uint16_t, 3> input_rgb = load_and_convert_image(argv[1]);
    // Just take the red channel
    Buffer<uint16_t, 2> input = input_rgb.sliced(2, 0);

    Buffer<uint16_t, 2> output(input.width(), input.height());
    int timing = atoi(argv[2]);

    stencil_chain(input, output);

    // Timing code

    BenchmarkConfig config;
    config.comparison_rounds = timing;

#ifndef NO_AUTO_SCHEDULE
    auto [manual, auto_scheduled] = benchmark_comparison(
        config,
        [&]() {
            stencil_chain(input, output);
            output.device_sync();
        },
        [&]() {
            stencil_chain_auto_schedule(input, output);
            output.device_sync();
        });
    printf("Manually-tuned time: %gms\n", manual.wall_time * 1e3);
    printf("Auto-scheduled time: %gms\n", auto_scheduled.wall_time * 1e3);
#else
    auto manual = benchmark(
        [&]() {
            stencil_chain(input, output);
            output.device_sync();
        },
        config);
    printf("Manually-tuned time: %gms\n", manual.wall_time * 1e3);
#endif

    convert_and_save_image(output, argv[3]);

    printf("Success!\n");
    return 0;
}
