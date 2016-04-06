#include <stdio.h>
#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {

    //int W = 64*3, H = 64*3;
    const int W = 128, H = 48;

    Image<uint16_t> in(W, H, 2);
    for (int c = 0; c < 2; c++) {
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                in(x, y, c) = rand() & 0xff;
            }
        }
    }

    Var x("x"), y("y");

    Func interleaved("interleaved");
    interleaved(x, y) = select(x%2 == 0, in(x, y, 0), in(x, y, 1));

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        interleaved.gpu_tile(x, y, 16, 16);
    } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        interleaved.hexagon().vectorize(x, 32);
    } else {
        Var xo("xo"), yo("yo");
        interleaved.tile(x, y, xo, yo, x, y, 8, 8).vectorize(x);
    }

    Image<uint16_t> out = interleaved.realize(W, H, target);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint16_t correct = x%2 == 0 ? in(x, y, 0) : in(x, y, 1);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), correct);
                return -1;
            }
        }
    }

    printf("Success!\n");

    return 0;

}
