#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::Internal;

TEST(TupleUpdateOps, SingleElementAddition) {
    Var x("x"), y("y");
    Func f("f");

    f(x, y) += Tuple(x + y);

    Realization result = f.realize({1024, 1024});
    Buffer<int> a = result[0];
    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            int correct_a = x + y;
            EXPECT_EQ(a(x, y), correct_a) << "result(" << x << ", " << y << ") = (" << a(x, y) << ") instead of (" << correct_a << ")";
        }
    }
}

TEST(TupleUpdateOps, MultipleOperations) {
    Var x("x"), y("y");
    Func f("f");

    f(x, y) += Tuple(4, 8);
    f(x, y) *= Tuple(x + y, x + 13);
    f(x, y) /= Tuple(2, 2);
    f(x, y) -= Tuple(x, y);

    Realization result = f.realize({1024, 1024});
    Buffer<int> a = result[0], b = result[1];
    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            int correct_a = x + 2 * y;
            int correct_b = 4 * (x + 13) - y;
            EXPECT_EQ(a(x, y), correct_a) << "result(" << x << ", " << y << ") = (" << a(x, y) << ", " << b(x, y) << ") instead of (" << correct_a << ", " << correct_b << ")";
            EXPECT_EQ(b(x, y), correct_b) << "result(" << x << ", " << y << ") = (" << a(x, y) << ", " << b(x, y) << ") instead of (" << correct_a << ", " << correct_b << ")";
        }
    }
}

TEST(TupleUpdateOps, ThreeDimensionalWithCasts) {
    Var x("x"), i("i"), j("j");
    Func f("f"), g("g");

    g(i, j) = i + j;

    f(x, _) = Tuple(cast<int16_t>(x), cast<int32_t>(g(_)));
    f(x, _) += Tuple(cast<int16_t>(2 * x), cast<int32_t>(x));

    Realization result = f.realize({100, 100, 100});
    Buffer<int16_t> a = result[0];
    Buffer<int32_t> b = result[1];
    for (int j = 0; j < a.channels(); j++) {
        for (int i = 0; i < a.height(); i++) {
            for (int x = 0; x < a.width(); x++) {
                int correct_a = 3 * x;
                int correct_b = x + i + j;
                EXPECT_EQ(a(x, i, j), correct_a) << "result(" << x << ", " << i << ", " << j << ") = (" << a(x, i, j) << ", " << b(x, i, j) << ") instead of (" << correct_a << ", " << correct_b << ")";
                EXPECT_EQ(b(x, i, j), correct_b) << "result(" << x << ", " << i << ", " << j << ") = (" << a(x, i, j) << ", " << b(x, i, j) << ") instead of (" << correct_a << ", " << correct_b << ")";
            }
        }
    }
}

TEST(TupleUpdateOps, SelfMultiplication) {
    Var x("x"), y("y");
    Func f("f");

    f(x, y) = Tuple(x + 13, x + y);
    f(x, y) *= f(x, y);

    Realization result = f.realize({1024, 1024});
    Buffer<int> a = result[0], b = result[1];
    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            int correct_a = (x + 13) * (x + 13);
            int correct_b = (x + y) * (x + y);
            EXPECT_EQ(a(x, y), correct_a) << "result(" << x << ", " << y << ") = (" << a(x, y) << ", " << b(x, y) << ") instead of (" << correct_a << ", " << correct_b << ")";
            EXPECT_EQ(b(x, y), correct_b) << "result(" << x << ", " << y << ") = (" << a(x, y) << ", " << b(x, y) << ") instead of (" << correct_a << ", " << correct_b << ")";
        }
    }
}

TEST(TupleUpdateOps, MixedOperationsWithSelfReference) {
    Var x("x"), y("y");
    Func f("f");

    f(x, y) = Tuple(x + y);
    f(x, y) += Tuple(x);
    f(x, y) *= f(x, y);

    Realization result = f.realize({1024, 1024});
    Buffer<int> a = result[0];
    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            int correct_a = (2 * x + y) * (2 * x + y);
            EXPECT_EQ(a(x, y), correct_a) << "result(" << x << ", " << y << ") = (" << a(x, y) << ") instead of (" << correct_a << ")";
        }
    }
}
