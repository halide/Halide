#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(ParallelAllocTest, Basic) {
    for (int i = 0; i < 20; i++) {
        Var x, y, z;
        Func f, g;

        g(x, y) = x * y;
        f(x, y) = g(x - 1, y) + g(x + 1, y);

        g.compute_at(f, y);
        f.parallel(y);

        Buffer<int> im = f.realize({8, 8});
        f.realize(im);

        for (int x = 0; x < 8; x++) {
            for (int y = 0; y < 8; y++) {
                EXPECT_EQ(im(x, y), (x - 1) * y + (x + 1) * y) << "im(" << x << ", " << y << ") = " << im(x, y);
            }
        }
    }
}
