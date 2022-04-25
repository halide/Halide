#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "tiled_blur.h"

// defined away to avoid requiring libpng, libjpeg everywhere;
// left in because useful for debugging and profiling.
#define SAVE_IMAGES 0

#if SAVE_IMAGES
#include "halide_image_io.h"
#endif

#define RUN_BENCHMARKS 0
#if RUN_BENCHMARKS
#include "halide_benchmark.h"
#endif

using namespace Halide::Runtime;

const int W = 80, H = 80;

int my_halide_trace(void *user_context, const halide_trace_event_t *ev) {
    if (ev->event == halide_trace_begin_realization) {
        assert(ev->dimensions == 6);
        int min_x = ev->coordinates[0], width = ev->coordinates[1];
        int min_y = ev->coordinates[2], height = ev->coordinates[3];
        int max_x = min_x + width - 1;
        int max_y = min_y + height - 1;
#if !RUN_BENCHMARKS
        printf("Using %d x %d input tile over [%d - %d] x [%d - %d]\n", width, height, min_x, max_x,
               min_y, max_y);
#endif
        assert(min_x >= 0 && min_y >= 0 && max_x < W && max_y < H);

        // The input is large enough that the boundary condition could
        // only ever apply on one side.
        assert(width == 33 || width == 34);
        assert(height == 33 || height == 34);
    }
    return 0;
}

Buffer<uint8_t, 3> buffer_factory_planar(int w, int h, int c) {
    return Buffer<uint8_t, 3>(w, h, c);
}

Buffer<uint8_t, 3> buffer_factory_interleaved(int w, int h, int c) {
    return Buffer<uint8_t, 3>::make_interleaved(w, h, c);
}

void test(Buffer<uint8_t, 3> (*factory)(int w, int h, int c)) {
    Buffer<uint8_t, 3> input = factory(W, H, 3);
    input.for_each_element([&](int x, int y, int c) {
        // Just an arbitrary color pattern with enough variation to notice the brighten + blur
        if (c == 0) {
            input(x, y, c) = (uint8_t)((x % 7) + (y % 3));
        } else if (c == 1) {
            input(x, y, c) = (uint8_t)(x + y);
        } else {
            input(x, y, c) = (uint8_t)((x * 5) + (y * 2));
        }
    });
    Buffer<uint8_t, 3> output = factory(W, H, 3);

    printf("Evaluating output over %d x %d in tiles of size 32 x 32\n", W, H);
    tiled_blur(input, output);

#if RUN_BENCHMARKS
    double t = Halide::Tools::benchmark(10, 100, [&]() {
        tiled_blur(input, output);
    });
    const float megapixels = (W * H) / (1024.f * 1024.f);
    printf("Benchmark: %d %d -> %f mpix/s\n", W, H, megapixels / t);
#endif

#if SAVE_IMAGES
    static int x = 0;
    Halide::Tools::save_image(input, "/tmp/tiled_input" + std::to_string(x) + ".png");
    Halide::Tools::save_image(output, "/tmp/tiled_output" + std::to_string(x) + ".png");
    ++x;
#endif
}

int main(int argc, char **argv) {
    halide_set_custom_trace(&my_halide_trace);

    printf("Testing planar buffer...\n");
    test(buffer_factory_planar);

    printf("Testing interleaved buffer...\n");
    test(buffer_factory_interleaved);

    printf("Success!\n");
    return 0;
}
