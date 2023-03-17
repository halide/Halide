#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    ImageParam in(UInt(8), 2);

    Var x, y;

    // Wrap the input in a dummy func so we can schedule it.
    Func in_func;
    in_func(x, y) = in(x, y);

    // Transpose
    Func out;
    out(x, y) = in_func(y, x);

    // Do a nested tiling of the output into 4x4 tiles of 16x16
    // pixels.  We'll make blockidy be the tile index, and blockidx be
    // the subtile index.
    Var xi, yi, xo, yo, xii, xio, yii, yio, tile_idx, subtile_idx;
    out.tile(x, y, xo, yo, xi, yi, 64, 64)
        .fuse(xo, yo, tile_idx)
        .tile(xi, yi, xio, yio, xii, yii, 16, 16)
        .fuse(xio, yio, subtile_idx)
        .gpu_blocks(subtile_idx, tile_idx)
        .gpu_threads(xii, yii);

    // Load a tile on input and store it into shared.
    in_func.compute_at(out, subtile_idx).gpu_threads(x, y);

    Buffer<uint8_t> input(256, 256);
    lambda(x, y, cast<uint8_t>(x * 17 + y)).realize(input);
    in.set(input);

    Buffer<uint8_t> output = out.realize({256, 256});

    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            uint8_t correct = y * 17 + x;
            if (output(x, y) != correct) {
                printf("output(%d, %d) = %d instead of %d\n",
                       x, y, output(x, y), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
