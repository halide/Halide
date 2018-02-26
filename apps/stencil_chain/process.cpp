#include <cstdio>
#include <chrono>

#include "stencil_chain.h"
#include "stencil_chain_auto_schedule_old.h"
#include "stencil_chain_auto_schedule.h"

#include "halide_benchmark.h"
#include "HalideBuffer.h"
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
    Buffer<uint16_t> input = load_and_convert_image(argv[1]);
    // Just take the red channel
    input.slice(2, 0);

    Buffer<uint16_t> output(input.width() - 128, input.height() - 128);
    output.set_min(64, 64);
    int timing = atoi(argv[2]);

    stencil_chain(input, output);

    // Timing code

    // Manually-tuned version
    double best_manual = benchmark(timing, 1, [&]() {
        stencil_chain(input, output);
    });
    printf("Manually-tuned time: %gms\n", best_manual * 1e3);

    // Old auto-scheduler version
    double best_auto_old = benchmark(timing, 1, [&]() {
        stencil_chain_auto_schedule_old(input, output);
    });
    printf("Old auto-scheduler time: %gms\n", best_auto_old * 1e3);

    // New auto-scheduler version
    double best_auto = benchmark(timing, 1, [&]() {
        stencil_chain_auto_schedule(input, output);
    });
    printf("New auto-scheduler time: %gms\n", best_auto * 1e3);

    convert_and_save_image(output, argv[3]);

    return 0;
}
