#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(TransitiveBoundsTest, Basic) {
    Func f, g;
    Var x;
    f(x) = x;
    g(x) = f(x);

    g.bound(x, 0, 4);

    // Should be ok to unroll x because it's bounded by a constant in its only consumer
    f.compute_root().unroll(x);

    g.realize({4});
}
