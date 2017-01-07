#include <stdio.h>
#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::JavaScript)) {
        printf("Skipping reuse_stack_alloc test for JavaScript as it does not make sense.\n");
        return 0;
    }

    Func f, g, h, k;
    Var x;

    // Create a simple function computed at root.
    f(x) = x;
    f.compute_root();

    g(x) = f(x);
    g.compute_root();

    // Create a function that uses an undefined buffer after f is
    // freed.
    h(x) = undef<int>();
    h(x) += g(x);
    h.compute_root();

    k(x) = h(x);
    k.compute_root();

    // Bound it so the allocations go on the stack.
    k.bound(x, 0, 16);

    Buffer<int> result = k.realize(16);
    for (int i = 0; i < result.width(); i++) {
        if (result(i) != 2 * i) {
            printf("Error! Allocation did not get reused at %d (%d != %d)\n", i, result(i), 2 * i);
            return -1;
        }
    }


    printf("Success!\n");
    return 0;
}
