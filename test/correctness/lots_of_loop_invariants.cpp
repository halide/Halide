#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(LotsOfLoopInvariantsTest, Basic) {
    // Stress-test LICM by hoisting lots of loop invariants
    Var x, y, c;

    const int N = 100;

    Expr e = 0;
    for (int i = 0; i < N; i++) {
        Expr invariant = (c + i) * (c + i);
        e += invariant * (x + i);
    }

    Func f;
    f(x, y, c) = e;

    Target t(get_jit_target_from_environment());
    if (t.has_gpu_feature()) {
        Var xi, yi;
        f.gpu_tile(x, y, xi, yi, 8, 8);
    }

    ASSERT_NO_THROW(f.realize({1024, 1024, 3}));
}
