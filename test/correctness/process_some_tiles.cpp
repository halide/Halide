#include "Halide.h"
#include <math.h>
#include <stdio.h>

using namespace Halide;

// A version of pow that tracks usage so we can check how many times it was called.
int call_count = 0;
extern "C" HALIDE_EXPORT_SYMBOL float my_powf(float x, float y) {
    call_count++;
    // We have to read from call_count, or for some reason apple clang removes it entirely.
    assert(call_count != -1);
    return powf(x, y);
}
HalideExtern_2(float, my_powf, float, float);

int main(int argc, char **argv) {
    // Brighten some tiles of an image, where that region is given by
    // a lower-res bitmap.

    ImageParam bitmap(Bool(), 2);
    ImageParam image(Float(32), 2);
    const int tile_size = 16;

    Var x("x"), y("y"), xi("xi"), yi("yi"), t("t");

    // Break the input into tiles.
    Func tiled("tiled");
    tiled(xi, yi, x, y) = image(x * tile_size + xi, y * tile_size + yi);

    // Brighten each tile of the image
    Func brighter("brighter");
    brighter(xi, yi, x, y) = my_powf(tiled(xi, yi, x, y), 0.8f);

    // Select either the brighter tile or the input tile depending on the bitmap
    Func output_tiles("output_tiles");
    output_tiles(xi, yi, x, y) = select(bitmap(x, y), brighter(xi, yi, x, y), tiled(xi, yi, x, y));

    // Collapse back down into 2D
    Func output("output");
    output(x, y) = output_tiles(x % tile_size, y % tile_size,
                                x / tile_size, y / tile_size);

    // Compute the output in tiles of the appropriate size to simplify
    // the mod and div above. Not important for the stage skipping behavior.
    output.bound(x, 0, (image.dim(0).extent() / tile_size) * tile_size)
        .bound(y, 0, (image.dim(0).extent() / tile_size) * tile_size)
        .tile(x, y, xi, yi, tile_size, tile_size);

    // Vectorize within tiles. We would also parallelize across tiles,
    // but that introduces a race condition in the call_count.
    output.vectorize(xi, 4);

    // Compute brighter per tile of output_tiles. This puts it inside
    // the loop over x and y, which makes the condition in the select
    // a constant. This is the important part of the schedule!
    brighter.compute_at(output_tiles, x);

    // Schedule output_tiles per output tile. This choice is unimportant.
    output_tiles.compute_at(output, x);

    output.compile_jit();

    Buffer<bool> bitmap_buf(10, 10);
    bitmap_buf.fill(false);
    bitmap_buf(5, 5) = true;
    bitmap.set(bitmap_buf);

    Buffer<float> image_buf = lambda(x, y, (sin(x + y) + 1) / 2).realize({10 * tile_size, 10 * tile_size});
    image.set(image_buf);

    call_count = 0;
    Buffer<float> result = output.realize({10 * tile_size, 10 * tile_size});

    // Force a reload of call_count
    my_powf(1, 1);
    call_count--;

    // Check the right number of calls to powf occurred
    if (call_count != tile_size * tile_size) {
        printf("call_count = %d instead of %d\n", call_count, tile_size * tile_size);
        return -1;
    }

    // Check the output is correct
    for (int y = 0; y < result.height(); y++) {
        for (int x = 0; x < result.width(); x++) {
            bool active = bitmap_buf(x / tile_size, y / tile_size);
            float correct = active ? my_powf(image_buf(x, y), 0.8f) : image_buf(x, y);
            if (fabs(correct - result(x, y)) > 0.001f) {
                printf("result(%d, %d) = %f instead of %f\n",
                       x, y, result(x, y), correct);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
