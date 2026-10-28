#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// MAX_COPY_DIMS was raised to 16 (from 4), so 5-dimensional GPU buffers can
// now be copied to/from the GPU without error; this is now a correctness
// test rather than an error test.
int main(int argc, char **argv) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    Func f;
    Var v0, v1, v2, v3, v4;

    f(v0, v1, v2, v3, v4) = v0 + 2 * v1 + 4 * v2 + 8 * v3 + 16 * v4;

    f.compute_root().gpu_blocks(v3, v4).gpu_threads(v1, v2);

    // Linearize into an output buffer
    Func g;
    g(v0) = f(v0 % 2, (v0 / 2) % 2, (v0 / 4) % 2, (v0 / 8) % 2, (v0 / 16) % 2);

    Buffer<int> result = g.realize({32});
    for (int i = 0; i < 32; i++) {
        if (result(i) != i) {
            printf("result(%d) = %d instead of %d\n", i, result(i), i);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
