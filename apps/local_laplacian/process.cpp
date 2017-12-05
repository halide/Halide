#include <cstdio>
#include <chrono>

#include "local_laplacian.h"
#include "local_laplacian_auto_schedule_old.h"
#include "local_laplacian_auto_schedule.h"

#include "halide_benchmark.h"
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
    int timing = atoi(argv[5]);

    local_laplacian(input, levels, alpha/(levels-1), beta, output);

    // Timing code

    // Manually-tuned version
    double best_manual = benchmark(timing, 1, [&]() {
        local_laplacian(input, levels, alpha/(levels-1), beta, output);
    });
    printf("Manually-tuned time: %gms\n", best_manual * 1e3);

    // Old auto-scheduler version
    double best_auto_old = benchmark(timing, 1, [&]() {
        local_laplacian_auto_schedule_old(input, levels, alpha/(levels-1), beta, output);
    });
    printf("Old auto-scheduler time: %gms\n", best_auto_old * 1e3);

    // New auto-scheduler version
    double best_auto = benchmark(timing, 1, [&]() {
        local_laplacian_auto_schedule(input, levels, alpha/(levels-1), beta, output);
    });
    printf("New auto-scheduler time: %gms\n", best_auto * 1e3);

    convert_and_save_image(output, argv[6]);

    return 0;
}
