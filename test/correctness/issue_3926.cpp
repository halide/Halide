#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(Issue3926Test, SpecializationTiling) {
    Func f("f"), g("g");
    Var x("x"), y("y");
    Var tx("tx"), ty("ty");
    Param<bool> param;

    f(x) = x;
    g(x, y) = f(x) + select(param, 1, 2);

    // g.gpu_tile(x, y, tx, ty, 8, 8, TailStrategy::GuardWithIf);
    g.specialize(param).tile(x, y, tx, ty, 8, 8, TailStrategy::GuardWithIf);
    g.specialize(!param).tile(x, y, tx, ty, 8, 8, TailStrategy::GuardWithIf);
    g.specialize_fail("Unknown");
    f.in().compute_at(g, tx);

    Buffer<int> out(34, 34);
    param.set(false);
    ASSERT_NO_THROW(g.realize(out));
}
