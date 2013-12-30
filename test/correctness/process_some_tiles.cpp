#include <Halide.h>
#include <stdio.h>
#include <math.h>

using namespace Halide;

// A version of pow that tracks usage so we can check how many times it was called.
#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

int call_count = 0;
extern "C" DLLEXPORT float my_powf(float x, float y) {
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

    // Brighten the image
    Func brighter("brighter");
    brighter(x, y) = my_powf(image(x, y), 0.8f);

    // Select either the brighter or the input depending on the bitmap
    Func output("output");
    output(x, y) = select(bitmap(x/tile_size, y/tile_size), brighter(x, y), image(x, y));

    // Compute the output in tiles of the appropriate size
    output.tile(x, y, xi, yi, tile_size, tile_size);

    // Vectorize within tiles. We would also parallelize across tiles,
    // but that introduces a race condition in the call_count.
    output.vectorize(xi, 4).fuse(x, y, t);

    // Compute brighter per output tile
    brighter.compute_at(output, t);

    // Assert that the output is a whole number of tiles. Otherwise
    // the tiles in the schedule don't match up to the tiles in the
    // algorithm, and you can't safely skip any work.
    output.bound(x, 0, (image.extent(0)/tile_size)*tile_size);
    output.bound(y, 0, (image.extent(1)/tile_size)*tile_size);
    output.compile_jit();

    Image<bool> bitmap_buf(10, 10);
    bitmap_buf(5, 5) = 1;
    bitmap.set(bitmap_buf);

    Image<float> image_buf = lambda(x, y, (sin(x+y)+1)/2).realize(10 * tile_size, 10 * tile_size);
    image.set(image_buf);

    call_count = 0;
    Image<float> result = output.realize(10 * tile_size, 10 * tile_size);

    // Force a reload of call_count 
    my_powf(1, 1);
    call_count--;

    // Check the right number of calls to powf occurred
    if (call_count != tile_size*tile_size) {
        printf("call_count = %d instead of %d\n", call_count, tile_size * tile_size);
        //return -1;
    }

    // Check the output is correct
    for (int y = 0; y < result.height(); y++) {
        for (int x = 0; x < result.width(); x++) {
            bool active = bitmap_buf(x/tile_size, y/tile_size);
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
