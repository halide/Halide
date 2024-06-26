#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;
    Var xo, xi, yo, yi;

    Func f, g;

    printf("Defining function...\n");

    f(x, y) = cast<int>(x);
    g(x, y) = f(x + 1, y) + f(x - 1, y);

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        Var xo, yo, xi, yi;
        g.gpu_tile(x, y, xo, yo, xi, yi, 8, 8);
        f.compute_at(g, xo).gpu_threads(x, y).store_in(MemoryType::GPUShared);
    }

    printf("Realizing function...\n");

    Buffer<int> im = g.realize({32, 32}, target);

    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            if (im(i, j) != 2 * i) {
                printf("im[%d, %d] = %d (expected %d)\n", i, j, im(i, j), 2 * i);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
