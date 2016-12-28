#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("Not running test because no gpu target enabled\n");
        return 0;
    }

    Func f("f"), g("g");
    Var x("x"), xi("xi");

    f(x) = x;
    g(x) = f(x) + f(2*x);

    g.gpu_tile(x, xi, 16);
    f.compute_at(g, x).gpu_threads(x);

    // The amount of shared memory required varies with x

    Buffer<int> out = g.realize(100);
    for (int x = 0; x < 100; x++) {
        int correct = 3*x;
        if (out(x) != correct) {
            printf("out(%d) = %d instead of %d\n",
                   x, out(x), correct);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
