#include <cstdio>
#include <random>

#include "iir_blur.h"
#ifndef NO_AUTO_SCHEDULE
#include "iir_blur_auto_schedule.h"
#include "iir_blur_gradient_auto_schedule.h"
#endif

#include "benchmark_util.h"
#include "HalideBuffer.h"

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s in out\n", argv[0]);
        return 1;
    }

    const float alpha = 0.5f;

    Halide::Runtime::Buffer<float> input = load_and_convert_image(argv[1]);
    Halide::Runtime::Buffer<float> output(input.width(), input.height(), input.channels());

    multi_way_bench({
        {"iir_blur_generator Manual", [&]() { iir_blur(input, alpha, output); output.device_sync(); }},
    #ifndef NO_AUTO_SCHEDULE
        {"iir_blur_generator Auto-scheduled", [&]() { iir_blur_auto_schedule(input, alpha, output); output.device_sync(); }},
        {"iir_blur_generator Gradient auto-scheduled", [&]() { iir_blur_gradient_auto_schedule(input, alpha, output); output.device_sync(); }}
    #endif
    });

    convert_and_save_image(output, argv[2]);

    printf("Success!\n");

    return 0;
}
