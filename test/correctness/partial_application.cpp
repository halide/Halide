#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(PartialApplicationTest, Basic) {
    Var x, y;
    Func f, g;

    f(x, y) = 2.0f;

    // implicit for all y
    g(x, _) = f(x, _) + f(x - 1, _);

    // implicit for all x, y on both sides, except for the float which has zero implicit args
    Func h;
    h(_) = (g(_) + f(_)) * 6.0f;

    Buffer<float> im = h.realize({4, 4});

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            EXPECT_EQ(im(x, y), 36.0f) << "im(" << x << ", " << y << ") = " << im(x, y);
        }
    }
}
