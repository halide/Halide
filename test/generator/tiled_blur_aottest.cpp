#include <math.h>
#include <stdio.h>
#include "HalideRuntime.h"
#include "HalideBuffer.h"
#include <assert.h>

#include "tiled_blur.h"

using namespace Halide;

const int W = 80, H = 80;

int my_halide_trace(void *user_context, const halide_trace_event *ev) {
    if (ev->event == halide_trace_begin_realization) {
        assert(ev->dimensions == 6);
        int min_x = ev->coordinates[0], width = ev->coordinates[1];
        int min_y = ev->coordinates[2], height = ev->coordinates[3];
        int max_x = min_x + width - 1;
        int max_y = min_y + height - 1;
        printf("Using %d x %d input tile over [%d - %d] x [%d - %d]\n", width, height, min_x, max_x,
               min_y, max_y);
        assert(min_x >= 0 && min_y >= 0 && max_x < W && max_y < H);

        // The input is large enough that the boundary condition could
        // only ever apply on one side.
        assert(width == 33 || width == 34);
        assert(height == 33 || height == 34);
    }
    return 0;
}

Buffer<> buffer_factory_planar(halide_type_t t, int w, int h, int c) {
    return Buffer<>(t, w, h, c);
}

Buffer<> buffer_factory_interleaved(halide_type_t t, int w, int h, int c) {
    return Buffer<>::make_interleaved(t, w, h, c);
}

void test(Buffer<> (*factory)(halide_type_t, int w, int h, int c)) {
    Buffer<int> input = factory(halide_type_of<int>(), W, H, 3);
    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            input(x, y) = (int)(x * y);
        }
    }
    Buffer<float> output = factory(halide_type_of<float>(), W, H, 3);

    printf("Evaluating output over %d x %d in tiles of size 32 x 32\n", W, H);
    tiled_blur(input, output);
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
