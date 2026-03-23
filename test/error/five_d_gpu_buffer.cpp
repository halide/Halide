#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    // Move this test to correctness once we can support >4d buffer_ts on the gpu

    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        // This test is currently expected to error out. This is for the Makefile's benefit.
        printf("Error: pretending that there was an error\n");
        return 1;
    }

    Func f;
    Var v0, v1, v2, v3, v4;

    f(v0, v1, v2, v3, v4) = v0 + 2 * v1 + 4 * v2 + 8 * v3 + 16 * v4;

    f.compute_root().gpu_blocks(v3, v4).gpu_threads(v1, v2);

    // Linearize into an output buffer
    Func g;
    g(v0) = f(v0 % 2, (v0 / 2) % 2, (v0 / 4) % 2, (v0 / 8) % 2, (v0 / 16) % 2);

    Buffer<int> result = g.realize({32});

    // Delete this code once this test works.
    printf("Error: I should not have successfully compiled.\n");
    return 1;

    for (int i = 0; i < result.width(); i++) {
        if (i != result(i)) {
            printf("result(%d) = %d instead of %d\n",
                   i, result(i), i);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
