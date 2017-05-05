#include <cstdio>
#include <chrono>

#include "local_laplacian.h"
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

    Buffer<uint16_t> input = load_image(argv[1]);
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

    // Auto-scheduled version
    double best_auto = benchmark(timing, 1, [&]() {
        local_laplacian_auto_schedule(input, levels, alpha/(levels-1), beta, output);
    });
    printf("Auto-scheduled time: %gms\n", best_auto * 1e3);

    save_image(output, argv[6]);

    const halide_filter_metadata_t *md = local_laplacian_metadata();
    // Only compare the performance if target has non-gpu features.
    if (!strstr(md->target, "cuda") &&
        !strstr(md->target, "opencl") &&
        !strstr(md->target, "metal") &&
        (best_auto > best_manual * 2)) {
        printf("Auto-scheduler is much much slower than it should be.\n");
        return -1;
    }

    return 0;
}
