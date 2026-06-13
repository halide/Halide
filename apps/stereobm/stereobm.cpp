#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "stereobm.h"
#include "stereobm_auto_schedule.h"

#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 4) {
        printf("Usage: %s left right out\n", argv[0]);
        return 1;
    }

    Halide::Runtime::Buffer<uint8_t, 3> left = load_and_convert_image(argv[1]);
    Halide::Runtime::Buffer<uint8_t, 3> right = load_and_convert_image(argv[2]);
    Halide::Runtime::Buffer<int32_t, 2> output(left.width(), left.height());

    double best_manual = benchmark([&]() {
        stereobm(left, right, output);
        output.device_sync();
    });
    printf("Manually-tuned time: %gms\n", best_manual * 1e3);

    double best_auto = benchmark([&]() {
        stereobm_auto_scheudle(left, right, output);
        output.device_sync();
    });
    printf("Auto-scheduled time: %gms\n", best_auto * 1e3);

    convert_and_save_image(output, argv[2]);

    printf("Success!\n");
    return 0;
}
