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

#include "box_blur_pyramid_f32.h"
#include "box_blur_pyramid_u16.h"
#include "box_blur_pyramid_u8.h"

#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s in out\n", argv[0]);
        return 1;
    }

    Halide::Runtime::Buffer<uint8_t> input = load_and_convert_image(argv[1]);

    Halide::Runtime::Buffer<uint8_t> output8(input.width(), input.height());
    output8.fill(0);
    Halide::Runtime::Buffer<uint16_t> output16(input.width(), input.height());
    output16.fill(0);
    Halide::Runtime::Buffer<float> output32(input.width(), input.height());
    output32.fill(0);

    const bool save_output = output8.number_of_elements() <= 1024 * 1024;

    const int max_radius = 2048;
    Halide::Runtime::Buffer<uint8_t> padded8(input.width() + max_radius * 2,
                                             input.height() + max_radius * 2);
    padded8.fill(0);
    padded8.set_min(-max_radius, -max_radius);
    padded8.cropped(0, 0, input.width()).cropped(1, 0, input.height()).copy_from(input);

    auto padded16 = Halide::Runtime::Buffer<uint16_t>::make_with_shape_of(padded8);
    padded16.for_each_value([](uint16_t &x, uint8_t y) { x = y * 256; }, padded8);

    auto padded32 = Halide::Runtime::Buffer<float>::make_with_shape_of(padded8);
    padded32.for_each_value([](float &x, uint8_t y) { x = y / 255.0f; }, padded8);

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

        double best_manual = benchmark(3, 3, [&]() {
            box_blur(padded, r, output.width(), output.height(), scratch, output);
            output.device_sync();
        });
        printf("Box blur (recursive) (%d): %gms\n", r, best_manual * 1e3);
    }
    */

    auto throughput = [&](int r, double seconds) {
        return (output8.width()) * (output8.height()) / (1000000 * seconds);
    };

    std::vector<int> radii = {1, 2, 3};
    for (int r = 1; r < 256; r *= 2) {
        radii.push_back(4 * r);
        radii.push_back(5 * r);
        radii.push_back(6 * r);
        radii.push_back(7 * r);
    }

    radii.clear();
    radii.push_back(25);

    for (int r : radii) {
        float t8 = 0, t16 = 0, t32 = 0;

        // Compute the output in tiles in x. We do the tiling outside
        // of Halide because inside Halide we're going to do a
        // sum-scan over x, and the indexing gets complicated.
        const int xtile = output8.width() / 4096 + 1;

        // The output width may not be a multiple of the number of
        // tiles. If we overlap the tiles slightly, then to reach the
        // end of an output of width w exactly we must have:
        //
        // w == tile_stride * (xtile - 1) + tile_width
        //
        // where tile_width >= tile_stride
        //
        // This is satisfied by:
        const int tile_stride = output8.width() / xtile;
        const int tile_width = output8.width() - tile_stride * (xtile - 1);

        assert(output8.width() == tile_stride * (xtile - 1) + tile_width);

        const int tile_height = 512;

        auto tile = [&](halide_buffer_t *buf) {
            buf->dimensions++;
            buf->dim[2] = buf->dim[1];
            buf->dim[0].extent = tile_width;
            buf->dim[1].min = 0;
            buf->dim[1].extent = xtile;
            buf->dim[1].stride = tile_stride;
        };
        if (1) {
            auto translated = padded8;
            translated.set_min(r - max_radius, r - max_radius);
            translated.crop(0, 0, output8.width() + 2 * r);
            translated.crop(1, 0, output8.height() + 2 * r);
            auto out_window = output8;
            tile(out_window);
            double best_manual = benchmark(3, 3, [&]() {
                for (int y = 0; y < output8.height(); y += tile_height) {
                    auto iw = translated;
                    iw.translate(1, -y);
                    iw.crop(1, 0, tile_height + 2 * r);
                    auto ow = out_window;
                    ow.translate(2, -y);
                    ow.crop(2, 0, tile_height);
                    box_blur_pyramid_u8(iw, 2 * r + 1, tile_width, tile_stride, ow);
                }
            });
            t8 = throughput(r, best_manual);
            if (save_output) {
                convert_and_save_image(output8, "out_8_pyramid_" + std::to_string(r) + ".png");
            }
        }
        if (1) {
            auto translated = padded16;
            translated.set_min(r - max_radius, r - max_radius);
            translated.crop(0, 0, output8.width() + 2 * r);
            translated.crop(1, 0, output8.height() + 2 * r);
            auto out_window = output16;
            tile(out_window);
            double best_manual = benchmark(3, 3, [&]() {
                for (int y = 0; y < output8.height(); y += tile_height) {
                    auto iw = translated;
                    iw.translate(1, -y);
                    iw.crop(1, 0, tile_height + 2 * r);
                    auto ow = out_window;
                    ow.translate(2, -y);
                    ow.crop(2, 0, tile_height);
                    box_blur_pyramid_u16(iw, 2 * r + 1, tile_width, tile_stride, ow);
                }
            });
            t16 = throughput(r, best_manual);
            if (save_output) {
                convert_and_save_image(output16, "out_16_pyramid_" + std::to_string(r) + ".png");
            }
        }
        if (1) {
            auto translated = padded32;
            translated.set_min(r - max_radius, r - max_radius);
            translated.crop(0, 0, output8.width() + 2 * r);
            translated.crop(1, 0, output8.height() + 2 * r);
            auto out_window = output32;
            tile(out_window);
            double best_manual = benchmark(3, 3, [&]() {
                for (int y = 0; y < output8.height(); y += tile_height) {
                    auto iw = translated;
                    iw.translate(1, -y);
                    iw.crop(1, 0, tile_height + 2 * r);
                    auto ow = out_window;
                    ow.translate(2, -y);
                    ow.crop(2, 0, tile_height);
                    box_blur_pyramid_f32(iw, 2 * r + 1, tile_width, tile_stride, ow);
                }
            });
            t32 = throughput(r, best_manual);
            if (save_output) {
                convert_and_save_image(output32, "out_32_pyramid_" + std::to_string(r) + ".png");
            }
        }

        printf("Box blur (pyramid) (%4d): %6.1f %6.1f %6.1f\n", 2 * r + 1, t8, t16, t32);
    }

    // return 0;

    printf("Box blur (incremental)...\n");
    for (int r : radii) {
        const int N = 8;

        double best_manual = benchmark(3, 3, [&]() {
            int slices = 16;  // set this to num_cores
            int slice_size = (output8.height() + slices - 1) / slices;
            slice_size = (slice_size + N - 1) / N * N;

            struct Task {
                int N, r, slice_size;
                Halide::Runtime::Buffer<uint8_t> &padded;
                Halide::Runtime::Buffer<uint8_t> &output;
            } task{N, r, slice_size, padded8, output8};

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
        if (save_output) {
            convert_and_save_image(output8, "out_" + std::to_string(r) + ".png");
        }
    }

    /*
    for (int r = 1; r < 256; r *= 2) {

        double best_manual = benchmark(3, 3, [&]() {
            box_blur_log(input, r, output);
            output.device_sync();
        });
        printf("Box blur (sparse/dense) (%d): %gms\n", r, best_manual * 1e3);
    }

    printf("Success!\n");
    */
    return 0;
}
