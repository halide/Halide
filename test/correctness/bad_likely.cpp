#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(BadLikelyTest, Basic) {
    Func f;
    Var x;
    // Use a likely intrinsic to tag a disjoint range.
    f(x) = select(x < 10 || x > 20, likely(1), 2);

    Buffer<int> im = f.realize({30});
    for (int x = 0; x < 30; x++) {
        int correct = (x < 10 || x > 20) ? 1 : 2;
        EXPECT_EQ(im(x), correct) << "im(" << x << ") = " << im(x) << " instead of " << correct;
    }
}
