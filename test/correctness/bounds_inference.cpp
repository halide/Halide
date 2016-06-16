#include <stdio.h>
#include "Halide.h"
using namespace Halide;

int main(int argc, char **argv) {

    Func f, g, h; Var x, y;

    h(x) = x;
    g(x) = h(x-1) + h(x+1);
    f(x, y) = (g(x-1) + g(x+1)) + y;

    h.compute_root();
    g.compute_root();

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        f.gpu_tile(x, y, 16, 16);
        g.gpu_tile(x, 128);
        h.gpu_tile(x, 128);
    } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.hexagon().vectorize(x, 32);
        g.hexagon().vectorize(x, 32);
        h.hexagon().vectorize(x, 32);
    }

    Image<int> out = f.realize(32, 32, target);

    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            if (out(x, y) != x*4 + y) {
                printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), x*4+y);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
