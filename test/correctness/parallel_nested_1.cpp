#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y, z;
    Func f, g;

    Param<int> k;
    k.set(3);

    f(x, y, z) = x*y+z*k+1;
    g(x, y, z) = f(x, y, z) + 2;

    f.parallel(x);
    f.parallel(y);
    g.parallel(z);

    f.compute_at(g, z);

    auto target = get_jit_target_from_environment();
    if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        g.hexagon().vectorize(x, 32);
        f.vectorize(x, 32);
    }

    Buffer<int> im = g.realize(64, 64, 64);

    for (int x = 0; x < 64; x++) {
        for (int y = 0; y < 64; y++) {
            for (int z = 0; z < 64; z++) {
                if (im(x, y, z) != x*y+z*3+3) {
                    printf("im(%d, %d, %d) = %d\n", x, y, z, im(x, y, z));
                    return -1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
