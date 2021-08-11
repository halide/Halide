#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "box_blur.h"
#include "box_blur_incremental.h"
#include "box_blur_log.h"
#include "gaussian_blur.h"
#include "gaussian_blur_direct.h"

#include "box_blur_pyramid.h"

#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s in out\n", argv[0]);
        return 1;
    }

    Halide::Runtime::Buffer<uint8_t> input = load_and_convert_image(argv[1]);
    input.crop(1, 0, 512);

    Halide::Runtime::Buffer<uint8_t> output(input.width(), input.height());

    output.fill(0);

    int max_radius = 2048;
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

    for (int r = 1; r <= 1024; r *= 2) {
        double best_manual = benchmark([&]() {
            gaussian_blur(input, r, output);
            output.device_sync();
        });
        printf("Gaussian blur (recursive) (%d): %gms\n", r, best_manual * 1e3);
        convert_and_save_image(output, "out_" + std::to_string(r) + ".png");
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
    }
    */

    auto throughput = [&](int r, double seconds) {
        return (output.width() + r * 2 + 1) * (output.height() + r * 2 + 1) / (1000000 * seconds);
    };

    std::vector<int> radii = {1, 2, 3};
    for (int r = 1; r < 256; r *= 2) {
        radii.push_back(4 * r);
        radii.push_back(5 * r);
        radii.push_back(6 * r);
        radii.push_back(7 * r);
    }

    for (int r : radii) {
        Halide::Runtime::Buffer<uint8_t> translated = padded;
        translated.set_min(r - max_radius, r - max_radius);
        double best_manual = benchmark(30, 30, [&]() {
            box_blur_pyramid(translated, 2 * r + 1, output.width(), output);
            output.device_sync();
        });
        printf("Box blur (pyramid) (%d): %g\n", 2 * r + 1, throughput(r, best_manual));
        convert_and_save_image(output, "out_pyramid_" + std::to_string(r) + ".png");
    }

    printf("Box blur (incremental)...\n");
    for (int r : radii) {
        const int N = 8;

        double best_manual = benchmark(30, 30, [&]() {
            int slices = 16;  // set this to num_cores
            int slice_size = (output.height() + slices - 1) / slices;
            slice_size = (slice_size + N - 1) / N * N;

            struct Task {
                int N, r, slice_size;
                Halide::Runtime::Buffer<uint8_t> &padded;
                Halide::Runtime::Buffer<uint8_t> &output;
            } task{N, r, slice_size, padded, output};

            auto one_strip = [](void *ucon, int s, uint8_t *closure) {
                Task *t = (Task *)closure;
                const int N = t->N;
                const int w = t->output.width();
                const int r = t->r;
                Halide::Runtime::Buffer<uint32_t> scratch1(N, w + 2 * r + 1);
                Halide::Runtime::Buffer<uint32_t> scratch2(N, w + 2 * r + 1);
                scratch1.set_min(0, -1);
                scratch2.set_min(0, -1);
                int y_start = std::min(s * t->slice_size, t->output.height() - t->slice_size);
                int y_end = y_start + t->slice_size;
                bool valid = false;
                for (int y = y_start; y < y_end; y += N) {
                    Halide::Runtime::Buffer<uint8_t> in_slice =
                        t->padded
                            .cropped(0, -r, w + 2 * r + N * 2)
                            .cropped(1, y - r - 1, N + 2 * r + 1);
                    Halide::Runtime::Buffer<uint8_t> out_slice =
                        t->output.cropped(1, y, N);
                    in_slice.set_min(0, -1);
                    out_slice.set_min(0, 0);
                    box_blur_incremental(in_slice, scratch1, valid, r, w, scratch2, out_slice);
                    out_slice.device_sync();
                    valid = true;
                    std::swap(scratch1, scratch2);
                }
                return 0;
            };

            halide_do_par_for(nullptr, one_strip, 0, slices, (uint8_t *)&task);
        });
        printf("Box blur (incremental) (%d): %g\n", 2 * r + 1, throughput(r, best_manual));

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
