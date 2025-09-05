#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(VectorizeVaryingAllocationSizeTest, Basic) {
    Func f, g;
    Var x, xo, xi;

    f(x) = x;
    g(x) = f(x) + f(x * x - 20);

    g.split(x, xo, xi, 4).vectorize(xi);
    f.compute_at(g, xi);

    // The region required of f is [min(x, x*x-20), max(x, x*x-20)],
    // which varies nastily with the var being vectorized.

    Buffer<int> out = g.realize({100});

    for (int i = 0; i < 4; i++) {
        int correct = i + i * i - 20;
        EXPECT_EQ(out(i), correct) << "out(" << i << ") = " << out(i) << " instead of " << correct;
    }
}
