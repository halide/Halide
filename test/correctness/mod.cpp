#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

template<typename T>
class ModTest : public ::testing::Test {};

using TestTypes = ::testing::Types<float, double, int32_t, uint32_t, int16_t, uint16_t, int8_t, uint8_t>;
TYPED_TEST_SUITE(ModTest, TestTypes);

TYPED_TEST(ModTest, ModOperations) {
    using T = TypeParam;

    Var x;
    Func f;
    f(x) = cast<T>(x) % 2;

    Buffer<T> im = f.realize({16});

    for (int i = 0; i < 16; i++) {
        EXPECT_EQ(im(i), (T)(i % 2));
    }

    // Test for negative mod case. Modulous of a negative number by a
    // positive one in Halide is always positive and is such that the
    // same pattern repeats endlessly across the number line.
    // Like so:
    // x:     ... -7 -6 -5 -4 -3 -2 -1  0  1  2  3  4  5  6  7 ...
    // x % 4: ...  1  2  3  0  1  2  3  0  1  2  3  4  0  1  2 ...
    Func nf;
    nf(x) = cast<T>(-x) % 4;

    Buffer<T> nim = nf.realize({16});

    for (int i = 1; i < 16; i++) {
        EXPECT_EQ(nim(i), (T)((4 - (i % 4)) % 4));
    }
}
