#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x;

    Expr int_expr[4], uint_expr[4], double_expr, float_expr;
    for (int bits = 8, i = 0; bits <= 64; bits *= 2, i++) {
        int_expr[i] = cast(Int(bits), x);
        uint_expr[i] = cast(UInt(bits), x);
    }
    float_expr = cast<float>(x);
    double_expr = cast<double>(x);

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < i; j++) {
            assert((int_expr[i] + int_expr[j]).type() == int_expr[i].type());
            assert((uint_expr[i] + uint_expr[j]).type() == uint_expr[i].type());
            assert((int_expr[i] + uint_expr[j]).type() == int_expr[i].type());
            assert((uint_expr[i] + int_expr[j]).type() == int_expr[i].type());
        }

        assert((int_expr[i] + 1).type() == int_expr[i].type());
        assert((1 + int_expr[i]).type() == int_expr[i].type());
        assert((int_expr[i] + 1.0f).type() == Float(32));
        assert((uint_expr[i] + 1.0f).type() == Float(32));

        Expr a = int_expr[i];
        a += 1.0f + uint_expr[i];
        assert(a.type() == int_expr[i].type());
    }

    assert(float_expr.type() == Float(32));
    assert(double_expr.type() == Float(64));

    // Verify that broadcast-of-ramp works properly when cast
    {
        Type t = Int(32, 6);
        Expr r = Halide::Internal::Ramp::make(3, 7, 2);
        Expr b = Halide::Internal::Broadcast::make(r, 3);
        assert(b.type() == t);

        Type t_bool = UInt(1, 6);
        Expr b_bool = cast(t_bool, b);
        assert(b_bool.type() == t_bool);
    }

    printf("Success!\n");
    return 0;
}
