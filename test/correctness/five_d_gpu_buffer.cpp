#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    // Move this test to correctness once we can support >4d buffer_ts on the gpu

    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    return error_test("five_d_gpu_buffer", "Buffer has too many dimensions to copy to/from GPU", []() {
        Func f;
        Var v0, v1, v2, v3, v4;

        f(v0, v1, v2, v3, v4) = v0 + 2 * v1 + 4 * v2 + 8 * v3 + 16 * v4;

        f.compute_root().gpu_blocks(v3, v4).gpu_threads(v1, v2);

        // Linearize into an output buffer
        Func g;
        g(v0) = f(v0 % 2, (v0 / 2) % 2, (v0 / 4) % 2, (v0 / 8) % 2, (v0 / 16) % 2);

        Buffer<int> result = g.realize({32});
        (void)result;
    });
}
