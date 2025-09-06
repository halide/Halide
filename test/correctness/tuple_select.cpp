#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::Internal;

TEST(TupleSelect, TernaryWithExprCondition) {
    // ternary select with Expr condition
    Var x("x"), y("y");
    Func f("f");

    f(x, y) = select(x + y < 30, Tuple(x, y), Tuple(x - 1, y - 2));

    Realization result = f.realize({200, 200});
    Buffer<int> a = result[0], b = result[1];
    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            int correct_a = (x + y < 30) ? x : x - 1;
            int correct_b = (x + y < 30) ? y : y - 2;
            ASSERT_EQ(a(x, y), correct_a) << "result(" << x << ", " << y << ") a component incorrect";
            ASSERT_EQ(b(x, y), correct_b) << "result(" << x << ", " << y << ") b component incorrect";
        }
    }
}

TEST(TupleSelect, TernaryWithTupleCondition) {
    // ternary select with Tuple condition
    Var x("x"), y("y");
    Func f("f");

    f(x, y) = select(Tuple(x < 30, y < 30), Tuple(x, y), Tuple(x - 1, y - 2));

    Realization result = f.realize({200, 200});
    Buffer<int> a = result[0], b = result[1];
    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            int correct_a = (x < 30) ? x : x - 1;
            int correct_b = (y < 30) ? y : y - 2;
            ASSERT_EQ(a(x, y), correct_a) << "result(" << x << ", " << y << ") a component incorrect";
            ASSERT_EQ(b(x, y), correct_b) << "result(" << x << ", " << y << ") b component incorrect";
        }
    }
}

TEST(TupleSelect, MultiwayWithExprCondition) {
    // multiway select with Expr condition
    Var x("x"), y("y");
    Func f("f");

    f(x, y) = select(x + y < 30, Tuple(x, y),
                     x + y < 100, Tuple(x - 1, y - 2),
                     Tuple(x - 100, y - 200));

    Realization result = f.realize({200, 200});
    Buffer<int> a = result[0], b = result[1];
    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            int correct_a = (x + y < 30) ? x : ((x + y < 100) ? x - 1 : x - 100);
            int correct_b = (x + y < 30) ? y : ((x + y < 100) ? y - 2 : y - 200);
            ASSERT_EQ(a(x, y), correct_a) << "result(" << x << ", " << y << ") a component incorrect";
            ASSERT_EQ(b(x, y), correct_b) << "result(" << x << ", " << y << ") b component incorrect";
        }
    }
}

TEST(TupleSelect, MultiwayWithTupleCondition) {
    // multiway select with Tuple condition
    Var x("x"), y("y");
    Func f("f");

    f(x, y) = select(Tuple(x < 30, y < 30), Tuple(x, y),
                     Tuple(x < 100, y < 100), Tuple(x - 1, y - 2),
                     Tuple(x - 100, y - 200));

    Realization result = f.realize({200, 200});
    Buffer<int> a = result[0], b = result[1];
    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            int correct_a = (x < 30) ? x : ((x < 100) ? x - 1 : x - 100);
            int correct_b = (y < 30) ? y : ((y < 100) ? y - 2 : y - 200);
            ASSERT_EQ(a(x, y), correct_a) << "result(" << x << ", " << y << ") a component incorrect";
            ASSERT_EQ(b(x, y), correct_b) << "result(" << x << ", " << y << ") b component incorrect";
        }
    }
}
