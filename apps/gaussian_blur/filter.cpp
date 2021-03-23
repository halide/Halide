#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "gaussian_blur.h"
#include "gaussian_blur_direct.h"
#include "box_blur.h"
#include "box_blur_log.h"

#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s in out\n", argv[0]);
        return 1;
    }

    Halide::Runtime::Buffer<uint8_t> input = load_and_convert_image(argv[1]);
    Halide::Runtime::Buffer<uint8_t> output(input.width(), input.height());

    /*
    for (int r = 1; r < 20; r *= 2) {
        double best_manual = benchmark([&]() {
            gaussian_blur_direct(input, r, output);
            output.device_sync();
        });
        printf("Gaussian blur (direct) (%d): %gms\n", r, best_manual * 1e3);
    }

    for (int r = 1; r < 1024; r*=2) {
        double best_manual = benchmark([&]() {
            gaussian_blur(input, r, output);
            output.device_sync();
        });
        printf("Gaussian blur (recursive) (%d): %gms\n", r, best_manual * 1e3);
    }
    */

    for (int r = 1; r < 2048; r*=2) {

        Halide::Runtime::Buffer<uint32_t> scratch(nullptr, 0, 0);
        box_blur(input, r, scratch, output);
        scratch.allocate();
        printf("%d kilobytes of scratch\n", (int)(scratch.size_in_bytes() / 1024));

        double best_manual = benchmark(10, 10, [&]() {
                                                 box_blur(input, r, scratch, output);
            output.device_sync();
        });
        printf("Box blur (recursive) (%d): %gms\n", r, best_manual * 1e3);
    }

    /*
    for (int r = 1; r < 2048; r*=2) {

        double best_manual = benchmark(3, 3, [&]() {
                                           box_blur_log(input, r, output);
            output.device_sync();
        });
        printf("Box blur (recursive) (%d): %gms\n", r, best_manual * 1e3);
    }
    */

    convert_and_save_image(output, argv[2]);

    printf("Success!\n");
    return 0;
}
