#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature() && !target.has_feature(Target::OpenGLCompute)) {
        printf("No gpu target enabled. Skipping test.\n");
        return 0;
    }

    Func f, g, h, out;
    Var x, y, z;

    f(x, y, z) = x + y + z;
    f(x, y, z) += 1;
    g(x, y, z) = f(x, y, z);
    g(x, y, z) += 1;
    h(x, y, z) = g(x, y, z);
    h(x, y, z) += 1;
    out(x, y, z) = h(x, y, z);
    out(x, y, z) += 1;

    out.gpu_tile(x, y, z, 4, 4, 4);
    out.update().gpu_tile(x, y, 4, 4);
    h.compute_at(out, Var::gpu_blocks()).gpu_threads(x, y);
    h.update().gpu_threads(x);
    // Normally it isn't useful to schedule things at in-between
    // thread levels, so Halide doesn't expose a clean name for it.
    g.compute_at(h, Var("__thread_id_y")).gpu_threads(x);
    g.update();
    f.compute_at(g, Var::gpu_threads());
    f.update();

    Image<int> o = out.realize(64, 64, 64);

    for (int z = 0; z < 64; z++) {
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                int correct = x + y + z + 4;
                if (o(x, y, z) != correct) {
                    printf("out(%d, %d, %d) = %d instead of %d\n",
                           x, y, z, o(x, y, z), correct);
                    return -1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
