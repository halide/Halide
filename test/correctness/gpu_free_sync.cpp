#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(GPUFreeSync, Basic) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        GTEST_SKIP() << "No GPU target enabled.";
    }

    // Make sure that freeing GPU buffers doesn't occur before the
    // computation that is filling them completes.
    Func f;
    Var x, y, xi, yi;
    RDom r(0, 100);
    f(x, y) = sum(sqrt(sqrt(sqrt(sqrt(x + y + r)))));

    f.gpu_tile(x, y, xi, yi, 16, 16);

    // This allocates a buffer, does gpu compute into it, and then
    // frees it (calling dev_free) possibly before the compute is
    // done.
    for (int i = 0; i < 10; i++) {
        f.realize({1024, 1024});
    }
}
