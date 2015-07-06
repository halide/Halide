#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Target t(get_jit_target_from_environment());
    if (!t.has_gpu_feature()) {
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

    // Metal can't seem to deal with a 4x4x4 set of threads
    // TODO: Figure this out as it seems ok with a 16x16 one elsewhere
    // so it does not seem to be total size.
    int thread_per_dim = t.has_feature(Target::Metal) ? 3 : 4;
    out.gpu_tile(x, y, z, thread_per_dim, thread_per_dim, thread_per_dim);
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
