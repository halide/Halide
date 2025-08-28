#include "Halide.h"
#include <gtest/gtest.h>
#include <stdio.h>

using namespace Halide;

TEST(RealizeOverShiftedDomainTest, Basic) {
    Buffer<int> input(100, 50);

    // This image represents the range [100, 199]*[50, 99]
    input.set_min(100, 50);

    input(100, 50) = 123;
    input(198, 99) = 234;

    Func f;
    Var x, y;
    f(x, y) = input(2 * x, y / 2);

    f.compile_jit();

    // The output will represent the range from [50, 99]*[100, 199]
    Buffer<int> result(50, 100);
    result.set_min(50, 100);

    ASSERT_NO_THROW(f.realize(result));

    EXPECT_EQ(result(50, 100), 123);
    EXPECT_EQ(result(99, 199), 234);
}
