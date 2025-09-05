#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(ReuseStackAllocTest, Basic) {
    Func f, g, h, k;
    Var x;

    // Create a simple function computed at root.
    f(x) = x;
    f.compute_root();

    g(x) = f(x);
    g.compute_root();

    // Create a function that uses an undefined buffer after f is
    // freed.
    h(x) = undef<int>();
    h(x) += g(x);
    h.compute_root();

    k(x) = h(x);
    k.compute_root();

    // Bound it so the allocations go on the stack.
    k.bound(x, 0, 16);

    Buffer<int> result = k.realize({16});
    for (int i = 0; i < result.width(); i++) {
        EXPECT_EQ(result(i), 2 * i) << "Error! Allocation did not get reused at " << i << " (" << result(i) << " != " << (2 * i) << ")";
    }
}
