#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(UnrollDynamicLoopTest, Basic) {
    Func f, g;
    Var x;

    Buffer<float> in(100);
    in.for_each_element([&](int x) { in(x) = x * 2.0f; });

    f(x) = in(x) * 3;
    g(x) = f(x) * 2;

    Var xo, xi;
    g.split(x, xo, xi, 8, TailStrategy::GuardWithIf).unroll(xi);
    f.compute_at(g, xo).unroll(x).store_in(MemoryType::Stack);

    Buffer<float> result = g.realize({23});
    for (int i = 0; i < 23; i++) {
        float correct = i * 2 * 3 * 2;
        EXPECT_EQ(result(i), correct) << "result(" << i << ") = " << result(i) << " instead of " << correct;
    }
}
