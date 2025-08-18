#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(SplitReuseInnerNameBugTest, Basic) {
    Var x("x"), x0, x1, x2, x3;
    Func f("f");

    f(x) = 1;
    f.compute_root().split(x, x0, x, 16).split(x, x, x1, 2).split(x, x2, x, 4).split(x, x, x3, 2);
    f.realize({1024});
}
