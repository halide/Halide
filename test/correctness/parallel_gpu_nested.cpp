#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    Var x, y, z;
    Func f;

    Param<int> k;
    k.set(3);

    f(x, y, z) = x * y + z * k + 1;

    if (get_jit_target_from_environment().has_gpu_feature()) {
        Var xi, yi;
        f.gpu_tile(x, y, xi, yi, 16, 16);
    } else if (get_jit_target_from_environment().features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.hexagon(y);
    } else {
        printf("No gpu or hexagon target enabled. Skipping test.\n");
        return 0;
    }
    f.parallel(z);

    Buffer<int> im = f.realize(64, 64, 64);

    for (int x = 0; x < 64; x++) {
        for (int y = 0; y < 64; y++) {
            for (int z = 0; z < 64; z++) {
                if (im(x, y, z) != x * y + z * 3 + 1) {
                    printf("im(%d, %d, %d) = %d\n", x, y, z, im(x, y, z));
                    return -1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
