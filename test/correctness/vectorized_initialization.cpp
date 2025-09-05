#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(VectorizedInitializationTest, Basic) {
    // By default, the bounds computed in the initialization step of a
    // reduction cover all uses of the Func by later stages. During
    // lowering, we expand them to cover the bounds read by the update
    // step. We had a bug where we expanded the bounds, but didn't
    // updated the max_min, which meant that vectorized
    // initializations were not being initialized over the full
    // domain. This example tests the fix for that bug.

    Func f, g;
    Var x;
    RDom r(0, 4);

    f(x) = x;
    f(r) = f(r - 1) + f(r + 1);
    f.compute_root().vectorize(x, 4);
    f.update().unscheduled();

    g(x) = f(x);
    Buffer<int> result = g.realize({4});

    // The sequence generated should be:
    // -1, (-1 + 1) = 0, 0 + 2 = 2, 2 + 3 = 5, 5 + 4 = 9
    EXPECT_EQ(result(0), 0);
    EXPECT_EQ(result(1), 2);
    EXPECT_EQ(result(2), 5);
    EXPECT_EQ(result(3), 9);
}
