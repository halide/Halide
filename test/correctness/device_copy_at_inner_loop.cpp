#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {

    Target t = get_jit_target_from_environment();
    if (!t.has_gpu_feature()) {
        printf("[SKIP] no gpu feature enabled\n");
        return 0;
    }

    // Sliding window with the producer on the GPU and the consumer on
    // the CPU. This requires a copy inside the loop over which we are
    // sliding. Currently this copies the entire buffer back and
    // forth, which is suboptimal in the general case. In this
    // specific case we're folded over y, so copying the entire buffer
    // is not much more than just copying the part that was modified.

    Func f, g;
    Var x, y;

    f(x, y) = x + y;

    g(x, y) = f(x, y) + f(x, y + 1);

    Var xi, yi;
    f.store_root()
        .compute_at(g, y)
        .gpu_tile(x, xi, 32);

    Buffer<int> out = g.realize({100, 100});

    for (int y = 0; y < 100; y++) {
        for (int x = 0; x < 100; x++) {
            int correct = 2 * (x + y) + 1;
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
