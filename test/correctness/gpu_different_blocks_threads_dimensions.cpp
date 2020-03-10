#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("No gpu target enabled. Skipping test.\n");
        return 0;
    }

    Func f, g;
    Var x, y, xi, yi;
    f(x, y) = x + y;
    g(x, y) = f(x, y);

    // At one point in time, FuseGPUThreadLoops assumed that the
    // number of blocks dimensions matched the number of threads
    // dimensions. This test checks that things still work if they're
    // mismatched.

    g.tile(x, y, xi, yi, 16, 16)
        .gpu_blocks(y)
        .gpu_threads(xi, yi);

    f.compute_at(g, x)
        .store_in(MemoryType::Heap)
        .gpu_threads(x, y);

    g.compile_jit();

    printf("Success!\n");

    return 0;
}
