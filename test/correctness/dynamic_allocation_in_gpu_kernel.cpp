#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Target t(get_jit_target_from_environment());
    if (!t.has_gpu_feature()) {
        printf("Not running test because no gpu target enabled\n");
        return 0;
    }

    Func f, g;
    Var x, y;
    Param<int> p;

    f(x, y) = x + y;
    g(x, y) = f(x, y) + f(x + p, y + p);

    Var xi, yi;
    g.gpu_tile(x, y, xi, yi, 32, 16);
    f.compute_at(g, xi);

    g.specialize(p == 3);
    // For the p == 3 case, f's allocation is statically sized, so it
    // will go in local memory. For all other cases the allocation is
    // dynamically sized and per-thread so it will go in global memory
    // by default. It would also be legal to schedule it into shared,
    // but for p > 3 there isn't enough shared memory.

    constexpr int W = 128, H = 128;

    for (int i = 0; i < 10; i++) {
        p.set(i);
        Buffer<int> result = g.realize(W, H);
        result.copy_to_host();
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int correct = x + y + (x + i) + (y + i);
                int actual = result(x, y);
                if (correct != actual) {
                    printf("result(%d, %d) = %d instead of %d\n",
                           x, y, actual, correct);
                    return -1;
                }
            }
        }
    }

    printf("Success!\n");

    return 0;
}
