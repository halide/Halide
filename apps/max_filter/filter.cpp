#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "max_filter.h"
#include "max_filter_auto_schedule.h"

#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

void error_handler(void *, const char *msg) {
    printf("%s\n", msg);
    if (strstr(msg, "CUDA_ERROR_OUT_OF_MEMORY")) {
        printf("This GPU doesn't have sufficient memory to run this app. Exiting.\n");
        exit(0);
    } else {
        exit(1);
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s in out\n", argv[0]);
        return 1;
    }

    Halide::Runtime::Buffer<float> input = load_and_convert_image(argv[1]);
    Halide::Runtime::Buffer<float> output(input.width(), input.height(), 3);

    // The manual schedule uses ~360MB of GPU memory, which doesn't
    // seem like much, but is too much for some of our buildbots, so
    // we'll catch cuda out of memory errors here.
    halide_set_error_handler(error_handler);

    double best_manual = benchmark([&]() {
        max_filter(input, output);
        output.device_sync();
    });
    printf("Manually-tuned time: %gms\n", best_manual * 1e3);

    double best_auto = benchmark([&]() {
        max_filter_auto_schedule(input, output);
        output.device_sync();
    });
    printf("Auto-scheduled time: %gms\n", best_auto * 1e3);

    convert_and_save_image(output, argv[2]);

    return 0;
}
