#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(ParallelTest, Basic) {
    Var x;
    Func f;

    Param<int> k;
    k.set(3);

    f(x) = x * k;

    f.parallel(x);

    Buffer<int> im = f.realize({16});

    for (int i = 0; i < 16; i++) {
        EXPECT_EQ(im(i), i * 3) << "im(" << i << ") = " << im(i);
    }
}
