#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "box_blur.h"
#include "box_blur_incremental.h"
#include "box_blur_log.h"
#include "gaussian_blur.h"
#include "gaussian_blur_direct.h"

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

    int max_radius = 1024;
    Halide::Runtime::Buffer<uint8_t> padded(input.width() + max_radius * 2,
                                            input.height() + max_radius * 2);
    padded.fill(0);
    padded.set_min(-max_radius, -max_radius);
    padded.cropped(0, 0, input.width()).cropped(1, 0, input.height()).copy_from(input);

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
    /*
    for (int r = 1; r <= 512; r *= 2) {
        // Assume a padded input
        Halide::Runtime::Buffer<uint8_t> scratch(nullptr, 0, 0);
        box_blur(input, r, output.width(), output.height(), scratch, output);
        scratch.allocate();
        printf("%d kilobytes of scratch\n", (int)(scratch.size_in_bytes() / 1024));

        double best_manual = benchmark(10, 10, [&]() {
            box_blur(padded, r, output.width(), output.height(), scratch, output);
            output.device_sync();
        });
        printf("Box blur (recursive) (%d): %gms\n", r, best_manual * 1e3);

        convert_and_save_image(output, "out_" + std::to_string(r) + ".png");
    }
    */

    for (int r = 1; r <= 512; r *= 2) {
        const int N = 8;
        // Assume a padded input
        Halide::Runtime::Buffer<uint32_t> scratch1(N, output.width() + 2 * r + 1);
        Halide::Runtime::Buffer<uint32_t> scratch2(N, output.width() + 2 * r + 1);
        scratch1.set_min(0, -1);
        scratch2.set_min(0, -1);
        scratch1.fill(0);
        scratch2.fill(0);
        printf("%d kilobytes of scratch\n", (int)((scratch1.size_in_bytes() + scratch2.size_in_bytes()) / 1024));

        double best_manual = benchmark(20, 20, [&]() {
            bool valid = false;
            for (int y = 0; y < output.height(); y += N) {
                // FIXME: Shouldn't need a +N on the width of the padded slice
                Halide::Runtime::Buffer<uint8_t> in_slice =
                    padded.cropped(0, -r, output.width() + 2 * r + 2 * N).cropped(1, y - r - 1, N + 2 * r + 1);
                Halide::Runtime::Buffer<uint8_t> out_slice = output.cropped(1, y, N);
                in_slice.set_min(0, -1);
                out_slice.set_min(0, 0);
                box_blur_incremental(in_slice, scratch1, valid, r, output.width(), scratch2, out_slice);
                out_slice.device_sync();
                valid = true;
                std::swap(scratch1, scratch2);
            }
        });
        printf("Box blur (incremental) (%d): %gms\n", r, best_manual * 1e3);

        convert_and_save_image(output, "out_" + std::to_string(r) + ".png");
    }

    /*
    for (int r = 1; r < 256; r *= 2) {

        double best_manual = benchmark(10, 10, [&]() {
            box_blur_log(input, r, output);
            output.device_sync();
        });
        printf("Box blur (sparse/dense) (%d): %gms\n", r, best_manual * 1e3);
    }

    printf("Success!\n");
    */
    return 0;
}
