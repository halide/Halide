#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(GPUCPUSimultaneousRead, Basic) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        GTEST_SKIP() << "No GPU target enabled.";
    }

    Var x, y, xi, yi;
    ImageParam table(Int(32), 1);

    Func f, g, h;

    // It's possible to have a buffer simultaneously read on CPU and
    // GPU if a load from it gets lifted into a predicate used by skip
    // stages. This tests that path.

    f(x, y) = x * 2 + y + table(x);
    g(x, y) = x + y * 2 + table(y);
    h(x, y) = select(table(0) == 0, f(x, y), g(x, y));

    f.compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    g.compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    h.compute_root().gpu_tile(x, y, xi, yi, 8, 8);

    Buffer<int32_t> t(32);
    t.fill(17);
    t(0) = 0;
    table.set(t);
    Buffer<int32_t> result1 = h.realize({20, 20});
    t(0) = 1;
    table.set(t);
    Buffer<int32_t> result2 = h.realize({20, 20});

    for (int yy = 0; yy < 20; yy++) {
        for (int xx = 0; xx < 20; xx++) {
            int c1 = xx * 2 + yy + (xx == 0 ? 0 : 17);
            int c2 = xx + yy * 2 + (yy == 0 ? 1 : 17);
            ASSERT_EQ(result1(xx, yy), c1) << "result1(" << xx << ", " << yy << ")";
            ASSERT_EQ(result2(xx, yy), c2) << "result2(" << xx << ", " << yy << ")";
        }
    }
}
