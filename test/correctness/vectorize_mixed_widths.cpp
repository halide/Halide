#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(VectorizeMixedWidthsTest, Basic) {
    Var x("x");
    Func f("f"), g("g");

    f(x) = 2 * x;
    g(x) = f(x) / 2;

    Var xo, xi;
    f.compute_at(g, x).split(x, xo, xi, 16).vectorize(xi, 8).unroll(xi);
    g.compute_root().vectorize(x, 16);

    Buffer<int> r = g.realize({16});
    for (int i = 0; i < 16; i++) {
        EXPECT_EQ(r(i), i) << "Error at " << i << ": " << r(i);
    }
}
