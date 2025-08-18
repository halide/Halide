#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(CompareVarsTest, Basic) {
    Func f;
    Var x, y;
    f(x, y) = select(x == y, 1, 0);

    Buffer<int> im = f.realize({10, 10});

    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            int correct = (x == y) ? 1 : 0;
            EXPECT_EQ(im(x, y), correct) << "im(" << x << ", " << y << ") = " << im(x, y) << " instead of " << correct;
        }
    }

}
