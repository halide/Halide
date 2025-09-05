#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(VectorBoundsInferenceTest, Basic) {
    Func f("f"), g("g"), h("h");
    Var x, y;

    h(x) = x;
    g(x) = h(x - 1) + h(x + 1);
    f(x, y) = (g(x - 1) + g(x + 1)) + y;

    h.compute_root().vectorize(x, 4);
    g.compute_root().vectorize(x, 4);

    Buffer<int> out = f.realize({36, 2});

    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 36; x++) {
            EXPECT_EQ(out(x, y), x * 4 + y) << "out(" << x << ", " << y << ") = " << out(x, y) << " instead of " << (x * 4 + y);
        }
    }
}
