#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(BoundTest, Basic) {
    Var x, y, c;
    Func f, g, h;

    f(x, y) = max(x, y);
    g(x, y, c) = f(x, y) * c;

    g.bound(c, 0, 3);

    Buffer<int> imf = f.realize({32, 32});
    Buffer<int> img = g.realize({32, 32, 3});

    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            ASSERT_EQ(imf(i, j), (i > j ? i : j));
            for (int c = 0; c < 3; c++) {
                ASSERT_EQ(img(i, j, c), c * (i > j ? i : j));
            }
        }
    }
}
