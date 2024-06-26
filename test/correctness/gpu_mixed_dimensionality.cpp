#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    Func f("f"), g("g"), h("h"), out("out");
    Var x("x"), y("y"), z("z");

    f(x, y, z) = x + y + z;
    f(x, y, z) += 1;
    g(x, y, z) = f(x, y, z);
    g(x, y, z) += 1;
    h(x, y, z) = g(x, y, z);
    h(x, y, z) += 1;
    out(x, y, z) = h(x, y, z);
    out(x, y, z) += 1;

    Var xi("xi"), yi("yi"), zi("zi");
    out.gpu_tile(x, y, z, xi, yi, zi, 4, 4, 4);
    out.update().gpu_tile(x, y, xi, yi, 4, 4);
    h.compute_at(out, x).gpu_threads(x, y);
    h.update().gpu_threads(x);
    // TODO: NormalizeDimensionality in FuseGPUThreadLoops.cpp doesn't work in the following case.
    // g.compute_at(h, y).gpu_threads(x);
    // g.update();
    g.compute_at(h, x);
    g.update();
    f.compute_at(g, x);
    f.update();

    Buffer<int> o = out.realize({64, 64, 64});

    for (int z = 0; z < 64; z++) {
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                int correct = x + y + z + 4;
                if (o(x, y, z) != correct) {
                    printf("out(%d, %d, %d) = %d instead of %d\n",
                           x, y, z, o(x, y, z), correct);
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
