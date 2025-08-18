#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(CastTest, TypePromotionRules) {
    Var x{"x"};

    Expr int_expr[4], uint_expr[4];
    for (int bits = 8, i = 0; bits <= 64; bits *= 2, i++) {
        int_expr[i] = cast(Int(bits), x);
        uint_expr[i] = cast(UInt(bits), x);
    }

    const Expr float_expr = cast<float>(x);
    const Expr double_expr = cast<double>(x);

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < i; j++) {
            ASSERT_EQ((int_expr[i] + int_expr[j]).type(), int_expr[i].type());
            ASSERT_EQ((uint_expr[i] + uint_expr[j]).type(), uint_expr[i].type());
            ASSERT_EQ((int_expr[i] + uint_expr[j]).type(), int_expr[i].type());
            ASSERT_EQ((uint_expr[i] + int_expr[j]).type(), int_expr[i].type());
        }

        ASSERT_EQ((int_expr[i] + 1).type(), int_expr[i].type());
        ASSERT_EQ((1 + int_expr[i]).type(), int_expr[i].type());
        ASSERT_EQ((int_expr[i] + 1.0f).type(), Float(32));
        ASSERT_EQ((uint_expr[i] + 1.0f).type(), Float(32));

        Expr a = int_expr[i];
        a += 1.0f + uint_expr[i];
        ASSERT_EQ(a.type(), int_expr[i].type());
    }

    ASSERT_EQ(float_expr.type(), Float(32));
    ASSERT_EQ(double_expr.type(), Float(64));

    // Verify that broadcast-of-ramp works properly when cast
    {
        Expr r = Halide::Internal::Ramp::make(3, 7, 2);
        Expr b = Halide::Internal::Broadcast::make(r, 3);
        ASSERT_EQ(b.type(), Int(32, 6));

        Type t_bool = UInt(1, 6);
        Expr b_bool = cast(t_bool, b);
        ASSERT_EQ(b_bool.type(), t_bool);
    }
}
