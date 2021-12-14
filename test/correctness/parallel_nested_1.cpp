#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y, z;
    Func f, g;

    Param<int> k;
    k.set(3);

    f(x, y, z) = x * y + z * k + 1;
    g(x, y, z) = f(x, y, z) + 2;

    f.parallel(x);
    f.parallel(y);
    g.parallel(z);

    f.compute_at(g, z);

    auto target = get_jit_target_from_environment();
    if (target.has_feature(Target::HVX)) {
        g.hexagon().vectorize(x, 32);
        f.vectorize(x, 32);
    }
    printf("Using Target = %s\n", target.to_string().c_str());

    Buffer<int> im = g.realize({64, 64, 64}, target);

    for (int x = 0; x < 64; x++) {
        for (int y = 0; y < 64; y++) {
            for (int z = 0; z < 64; z++) {
                const int expected = x * y + z * 3 + 3;
                const int actual = im(x, y, z);
                if (actual != expected) {
                    fprintf(stderr, "im(%d, %d, %d) = %d, expected %d\n", x, y, z, actual, expected);
                    return -1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
