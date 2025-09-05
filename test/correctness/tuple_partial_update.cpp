#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::Internal;

TEST(TuplePartialUpdateTest, ArithmeticUpdates) {
    Var x("x"), y("y");
    Func f("f");

    f(x, y) = Tuple(x + y, undef<int32_t>());
    f(x, y)[0] += 3;
    f(x, y)[1] = x;
    f(x, y)[0] -= 1;
    f(x, y)[1] *= 4;
    f(x, y)[1] /= 2;

    Realization result = f.realize({1024, 1024});
    Buffer<int> a = result[0], b = result[1];
    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            int correct_a = x + y + 2;
            int correct_b = x * 2;
            EXPECT_EQ(a(x, y), correct_a) << "a(" << x << ", " << y << ")";
            EXPECT_EQ(b(x, y), correct_b) << "b(" << x << ", " << y << ")";
        }
    }
}

TEST(TuplePartialUpdateTest, ConditionalUpdate) {
    Var x("x"), y("y");
    Func f("f");

    f(x, y) = Tuple(x, y);
    f(x, y)[1] += select(x < 20, 20 * x, undef<int>());

    Realization result = f.realize({1024, 1024});
    Buffer<int> a = result[0], b = result[1];
    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            int correct_a = x;
            int correct_b = (x < 20) ? 20 * x + y : y;
            EXPECT_EQ(a(x, y), correct_a) << "a(" << x << ", " << y << ")";
            EXPECT_EQ(b(x, y), correct_b) << "b(" << x << ", " << y << ")";
        }
    }
}
