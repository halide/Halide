#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    Var x("x"), y("y");

    Func interleaved("interleaved");
    interleaved(x, y) = select(x % 2 == 0, cast<uint16_t>(3), cast<uint16_t>(7));

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        Var tx("tx"), ty("ty");
        interleaved.gpu_tile(x, y, tx, ty, 16, 16);
    } else if (target.has_feature(Target::HVX)) {
        interleaved.hexagon().vectorize(x, 64);
    } else {
        Var xo("xo"), yo("yo");
        interleaved.tile(x, y, xo, yo, x, y, 8, 8).vectorize(x);
    }

    Buffer<uint16_t> out = interleaved.realize({128, 128}, target);
    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            uint16_t correct = x % 2 == 0 ? 3 : 7;
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");

    return 0;
}
