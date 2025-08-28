#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(DeviceCopyAtInnerLoopTest, SlidingWindowGpuToCpu) {
    Target t = get_jit_target_from_environment();
    if (!t.has_gpu_feature()) {
        GTEST_SKIP() << "No GPU target enabled.";
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

    for (int yy = 0; yy < 100; yy++) {
        for (int xx = 0; xx < 100; xx++) {
            int correct = 2 * (xx + yy) + 1;
            EXPECT_EQ(out(xx, yy), correct) << "out(" << xx << ", " << yy << ")";
        }
    }
}
