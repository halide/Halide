#include "Halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;
using namespace Halide::Internal;

#define internal_assert _halide_user_assert

void check(Expr test, Expr expected, Type required_type) {
    Expr result = pattern_match_intrinsics(test);
    if (!equal(result, expected) || required_type != expected.type()) {
        std::cout << "failure!\n";
        std::cout << "test: " << test << "\n";
        std::cout << "result: " << result << "\n";
        std::cout << "exepcted: " << expected << "\n";
        abort();
    }
}

void check(Expr test, Expr expected) {
    return check(test, expected, expected.type());
}

int main(int argc, char **argv) {
    Expr xi = Variable::make(Int(8, 4), "xi");
    Expr yi = Variable::make(Int(8, 4), "yi");
    Expr zi = Variable::make(Int(8, 4), "wi");
    Expr wi = Variable::make(Int(8, 4), "zi");
    Expr xu = Variable::make(UInt(8, 4), "xu");
    Expr yu = Variable::make(UInt(8, 4), "yu");
    Expr zu = Variable::make(UInt(8, 4), "wu");
    Expr wu = Variable::make(UInt(8, 4), "zu");

    check(xi * 2, xi << 1);
    check(xu * 4, xu << 2);

    check(xi / 8, xi >> 3);
    check(xu / 4, xu >> 2);

    check(i16(xi) * 4096, widening_shift_left(xi, u8(12)));
    check(u16(xu) * 128, widening_shift_left(xu, u8(7)));
    check(u32(xu) * 256, u32(widening_shift_left(xu, u8(8))));

    //check(narrow((i16(xi) + 8) / 16), rounding_shift_right(xi, u8(4)));
    //check(narrow(widening_add(xi, i8(4)) / 8), rounding_shift_right(xi, u8(3)));
    check(saturating_add(xi, i8(32)) / 64, rounding_shift_right(xi, u8(6)));

    check(i16(xi) + yi, widening_add(xi, yi));
    check(u16(xu) + yu, widening_add(xu, yu));
    check(i16(xu) + yu, i16(widening_add(xu, yu)));

    check(i16(xi) - yi, widening_sub(xi, yi));
    check(i16(xu) - yu, widening_sub(xu, yu));

    check(i16(xi) * yi, widening_mul(xi, yi));
    check(u16(xu) * yu, widening_mul(xu, yu));
    check(i32(xi) * yi, i32(widening_mul(xi, yi)));
    check(u32(xu) * yu, u32(widening_mul(xu, yu)));

    // Widening mul allows mixed signs
    check(i16(xi) * yu, widening_mul(xi, yu), Int(16, 4));
    check(i32(xi) * yu, i32(widening_mul(xi, yu)));

    // Excessive widening should be moved outside the arithmetic.
    check(widening_mul(i16(xi), i16(yi)), i32(widening_mul(xi, yi)));
    check(widening_mul(u16(xu), u16(yu)), u32(widening_mul(xu, yu)));
    check(widening_mul(i16(xi), i16(yu)), i32(widening_mul(xi, yu)));
    check(widening_mul(i16(xu), i16(yi)), i32(widening_mul(xu, yi)));

    check(widening_add(i16(xi), i16(yi)), i32(widening_add(xi, yi)));
    check(widening_add(u16(xu), u16(yu)), u32(widening_add(xu, yu)));
    check(widening_add(i16(xu), i16(yu)), i32(widening_add(xu, yu)));


    check(i8_sat(i16(xi) + yi), saturating_add(xi, yi));
    check(u8_sat(u16(xu) + yu), saturating_add(xu, yu));
    check(u8(min(u16(xu) + u16(yu), 255)), saturating_add(xu, yu));
    check(u8(min(i16(xu) + i16(yu), 255)), saturating_add(xu, yu));
    check(u8(max(i16(xu) - i16(yu), 0)), saturating_sub(xu, yu));

    check(i8_sat(i16(xi) - yi), saturating_sub(xi, yi));

    check(i8((i16(xi) + yi) / 2), halving_add(xi, yi));
    check(u8((u16(xu) + yu) / 2), halving_add(xu, yu));
    check(i8(widening_add(xi, yi) / 2), halving_add(xi, yi));
    check(u8(widening_add(xu, yu) / 2), halving_add(xu, yu));

    check(i8((i16(xi) - yi) / 2), halving_sub(xi, yi));
    check(i8(widening_sub(xi, yi) / 2), halving_sub(xi, yi));

    check(i8((i16(xi) + yi + 1) / 2), rounding_halving_add(xi, yi));
    check(u8((u16(xu) + yu + 1) / 2), rounding_halving_add(xu, yu));
    check(i8((widening_add(xi, yi) + 1) / 2), rounding_halving_add(xi, yi));
    check(u8((widening_add(xu, yu) + 1) / 2), rounding_halving_add(xu, yu));

    check(i8((i16(xi) - yi + 1) / 2), rounding_halving_sub(xi, yi));
    check(i8((widening_sub(xi, yi) + 1) / 2), rounding_halving_sub(xi, yi));

    check(abs(i16(xi) - i16(yi)), u16(absd(xi, yi)));
    check(abs(i16(xu) - i16(yu)), u16(absd(xu, yu)));
    check(abs(widening_sub(xi, yi)), u16(absd(xi, yi)));
    check(abs(widening_sub(xu, yu)), u16(absd(xu, yu)));

    // Test combinations of multiplies and adds with widening
    // Same sign:
    check(i16(xi) * yi + i16(zi) * wi, widening_mul(xi, yi) + widening_mul(zi, wi));
    check(u16(xu) * yu + u16(zu) * wu, widening_mul(xu, yu) + widening_mul(zu, wu));
    check(i32(xi) * yi + i32(zi) * wi, widening_add(widening_mul(xi, yi), widening_mul(zi, wi)));
    check(u32(xu) * yu + u32(zu) * wu, widening_add(widening_mul(xu, yu), widening_mul(zu, wu)), UInt(32, 4));

    // Mixed signs:
    check(i16(xu) * yi + i16(zu) * wi, widening_mul(xu, yi) + widening_mul(zu, wi), Int(16, 4));

    // Widening multiply-adds involving constants should be rewritten to adds whenever possible:
    check(i16(xi) * 3 + i16(yi) * 5, widening_mul(xi, i8(3)) + widening_mul(yi, i8(5)));
    check(i16(xi) * 5 - i16(yi) * 127, widening_mul(xi, i8(5)) + widening_mul(yi, i8(-127)));
    check(i16(xu) * 3 - i16(yu) * 7, widening_mul(xu, i8(3)) + widening_mul(yu, i8(-7)));
    check(i32(xi) * 3 + i32(yi) * 5, widening_add(widening_mul(xi, i8(3)), widening_mul(yi, i8(5))));
    check(i32(xi) * 5 - i32(yi) * 3, widening_add(widening_mul(xi, i8(5)), widening_mul(yi, i8(-3))));
    check(i32(xu) * 7 - i32(yu) * 6, widening_add(widening_mul(xu, i8(7)), widening_mul(yu, i8(-6))));

    check((i16(xu) * 4 + i16(yu)) * 3, widening_mul(xu, i8(12)) + widening_mul(yu, i8(3)));
    check((i16(xi) + i16(yi) * 7) * 5, widening_mul(xi, i8(5)) + widening_mul(yi, i8(35)));
    check((i16(xu) * 4 - i16(yu)) * 3, widening_mul(xu, i8(12)) + widening_mul(yu, i8(-3)));
    check((i16(xi) - i16(yi) * 7) * 5, widening_mul(xi, i8(5)) + widening_mul(yi, i8(-35)));

    check((u16(xu) + u16(yu)) * 2, widening_shift_left(xu, u8(1)) + widening_shift_left(yu, u8(1)));
    check((u16(xu) - u16(yu)) * 2, widening_shift_left(xu, u8(1)) - widening_shift_left(yu, u8(1)));
    check((u16(xu) * 4 + u16(yu)) * 3, widening_mul(xu, u8(12)) + widening_mul(yu, u8(3)));
    check((u16(xu) + u16(yu) * 7) * 5, widening_mul(xu, u8(5)) + widening_mul(yu, u8(35)));
    check((u16(xu) * 4 - u16(yu)) * 3, widening_mul(xu, u8(12)) - widening_mul(yu, u8(3)));
    check((u16(xu) - u16(yu) * 7) * 5, widening_mul(xu, u8(5)) - widening_mul(yu, u8(35)));

    printf("Success!\n");
    return 0;
}
