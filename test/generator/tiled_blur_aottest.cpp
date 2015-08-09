#include <math.h>
#include <stdio.h>
#include "HalideRuntime.h"
#include <assert.h>

#include "tiled_blur.h"
#include "halide_image.h"

using namespace Halide::Tools;

const int W = 80, H = 80;

extern "C" int halide_trace(void *user_context, const halide_trace_event *ev) {
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

int main(int argc, char **argv) {
    Image<float> input(W, H);
    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            input(x, y) = (float)(x * y);
        }
    }
    Image<float> output(W, H);

    printf("Evaluating output over %d x %d in tiles of size 32 x 32\n", W, H);
    tiled_blur(input, output);

    printf("Success!\n");
    return 0;
}
