#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(UpdateChunkTest, Basic) {
    // This test computes a function within the update step of a reduction

    Func f, g;
    Var x, y, z;
    RDom r(0, 10);

    f(x, y) = x * y;
    g(x, y) = 0;
    g(x, r) = f(r, x) + 1;

    f.compute_at(g, r);
    g.realize({10, 10});
}
