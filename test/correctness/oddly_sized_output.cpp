#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(OddlySizedOutputTest, Basic) {
    Buffer<int> input(87, 93);
    input.fill(0);

    Func f;
    Var x, y;
    f(x, y) = input(x, y) * 2;

    Var yi;
    f.vectorize(x, 4).unroll(x, 3).unroll(x, 2);
    f.split(y, y, yi, 16).parallel(y);

    Buffer<int> out = f.realize({87, 93});

    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            EXPECT_EQ(out(x, y), input(x, y) * 2) << "out(" << x << ", " << y << ") = " << out(x, y) << " instead of " << (input(x, y) * 2);
        }
    }
}
