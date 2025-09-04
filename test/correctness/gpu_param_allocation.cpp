#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::Internal;

TEST(GPUParamAllocation, Basic) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        GTEST_SKIP() << "No GPU target enabled";
    }

    Func f("f"), g("g");
    Var x("x"), y("y");

    Param<int> slices;
    RDom r(0, 3 * slices + 1);
    slices.set_range(1, 256);

    f(x, y) = x + y;
    g(x, y) = sum(f(x, r)) + slices;

    Var xi("xi"), yi("yi");
    g.compute_root().gpu_tile(x, y, xi, yi, 16, 16);
    f.compute_at(g, xi);

    slices.set(32);
    ASSERT_NO_THROW(g.realize({1024, 1024}));
}
