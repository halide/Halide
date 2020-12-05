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
    Expr i8x = Variable::make(Int(8, 4), "i8x");
    Expr i8y = Variable::make(Int(8, 4), "i8y");
    Expr i8z = Variable::make(Int(8, 4), "i8w");
    Expr i8w = Variable::make(Int(8, 4), "i8z");
    Expr u8x = Variable::make(UInt(8, 4), "u8x");
    Expr u8y = Variable::make(UInt(8, 4), "u8y");
    Expr u8z = Variable::make(UInt(8, 4), "u8w");
    Expr u8w = Variable::make(UInt(8, 4), "u8z");
    Expr i32x = Variable::make(Int(32, 4), "i32x");
    Expr i32y = Variable::make(Int(32, 4), "i32y");

    check(i8x * 2, i8x << 1);
    check(u8x * 4, u8x << 2);

    check(i8x / 8, i8x >> 3);
    check(u8x / 4, u8x >> 2);

    check(i16(i8x) * 4096, widening_shift_left(i8x, u8(12)));
    check(u16(u8x) * 128, widening_shift_left(u8x, u8(7)));
    check(u32(u8x) * 256, u32(widening_shift_left(u8x, u8(8))));

    //check(narrow((i16(i8x) + 8) / 16), rounding_shift_right(i8x, u8(4)));
    //check(narrow(widening_add(i8x, i8(4)) / 8), rounding_shift_right(i8x, u8(3)));
    check(saturating_add(i8x, i8(32)) / 64, rounding_shift_right(i8x, u8(6)));
    check((i8x + i8(32)) / 64, (i8x + i8(32)) >> 6);  // Not a rounding_shift_right due to overflow.
    check((i32x + 16) / 32, rounding_shift_right(i32x, u32(5)));

    check(i16(i8x) + i8y, widening_add(i8x, i8y));
    check(u16(u8x) + u8y, widening_add(u8x, u8y));
    check(i16(u8x) + u8y, i16(widening_add(u8x, u8y)));

    check(i16(i8x) - i8y, widening_sub(i8x, i8y));
    check(i16(u8x) - u8y, widening_sub(u8x, u8y));

    check(i16(i8x) * i8y, widening_mul(i8x, i8y));
    check(u16(u8x) * u8y, widening_mul(u8x, u8y));
    check(i32(i8x) * i8y, i32(widening_mul(i8x, i8y)));
    check(u32(u8x) * u8y, u32(widening_mul(u8x, u8y)));

    // Widening mul allows mixed signs
    check(i16(i8x) * u8y, widening_mul(i8x, u8y), Int(16, 4));
    check(i32(i8x) * u8y, i32(widening_mul(i8x, u8y)));

    // Excessive widening should be moved outside the arithmetic.
    check(widening_mul(i16(i8x), i16(i8y)), i32(widening_mul(i8x, i8y)));
    check(widening_mul(u16(u8x), u16(u8y)), u32(widening_mul(u8x, u8y)));
    check(widening_mul(i16(i8x), i16(u8y)), i32(widening_mul(i8x, u8y)));
    check(widening_mul(i16(u8x), i16(i8y)), i32(widening_mul(u8x, i8y)));

    check(widening_add(i16(i8x), i16(i8y)), i32(widening_add(i8x, i8y)));
    check(widening_add(u16(u8x), u16(u8y)), u32(widening_add(u8x, u8y)));
    check(widening_add(i16(u8x), i16(u8y)), i32(widening_add(u8x, u8y)));


    check(i8_sat(i16(i8x) + i8y), saturating_add(i8x, i8y));
    check(u8_sat(u16(u8x) + u8y), saturating_add(u8x, u8y));
    check(u8(min(u16(u8x) + u16(u8y), 255)), saturating_add(u8x, u8y));
    //check(u8(min(i16(u8x) + i16(u8y), 255)), saturating_add(u8x, u8y));

    check(i8_sat(i16(i8x) - i8y), saturating_sub(i8x, i8y));
    check(u8(max(i16(u8x) - i16(u8y), 0)), saturating_sub(u8x, u8y));

    check(i8((i16(i8x) + i8y) / 2), halving_add(i8x, i8y));
    check(u8((u16(u8x) + u8y) / 2), halving_add(u8x, u8y));
    check(i8(widening_add(i8x, i8y) / 2), halving_add(i8x, i8y));
    check(u8(widening_add(u8x, u8y) / 2), halving_add(u8x, u8y));
    check((i32x + i32y) / 2, halving_add(i32x, i32y));

    check(i8((i16(i8x) - i8y) / 2), halving_sub(i8x, i8y));
    check(i8(widening_sub(i8x, i8y) / 2), halving_sub(i8x, i8y));
    check((i32x - i32y) / 2, halving_sub(i32x, i32y));

    check(i8((i16(i8x) + i8y + 1) / 2), rounding_halving_add(i8x, i8y));
    check(u8((u16(u8x) + u8y + 1) / 2), rounding_halving_add(u8x, u8y));
    check(i8((widening_add(i8x, i8y) + 1) / 2), rounding_halving_add(i8x, i8y));
    check(u8((widening_add(u8x, u8y) + 1) / 2), rounding_halving_add(u8x, u8y));
    check((i32x + i32y + 1) / 2, rounding_halving_add(i32x, i32y));

    check(i8((i16(i8x) - i8y + 1) / 2), rounding_halving_sub(i8x, i8y));
    check(i8((widening_sub(i8x, i8y) + 1) / 2), rounding_halving_sub(i8x, i8y));
    check((i32x - i32y + 1) / 2, rounding_halving_sub(i32x, i32y));

    check(abs(i16(i8x) - i16(i8y)), u16(absd(i8x, i8y)));
    check(abs(i16(u8x) - i16(u8y)), u16(absd(u8x, u8y)));
    check(abs(widening_sub(i8x, i8y)), u16(absd(i8x, i8y)));
    check(abs(widening_sub(u8x, u8y)), u16(absd(u8x, u8y)));

    // Test combinations of multiplies and adds with widening
    // Same sign:
    check(i16(i8x) * i8y + i16(i8z) * i8w, widening_mul(i8x, i8y) + widening_mul(i8z, i8w));
    check(u16(u8x) * u8y + u16(u8z) * u8w, widening_mul(u8x, u8y) + widening_mul(u8z, u8w));
    check(i32(i8x) * i8y + i32(i8z) * i8w, widening_add(widening_mul(i8x, i8y), widening_mul(i8z, i8w)));
    check(u32(u8x) * u8y + u32(u8z) * u8w, widening_add(widening_mul(u8x, u8y), widening_mul(u8z, u8w)), UInt(32, 4));

    // Mixed signs:
    check(i16(u8x) * i8y + i16(u8z) * i8w, widening_mul(u8x, i8y) + widening_mul(u8z, i8w), Int(16, 4));

    // Widening multiply-adds involving constants should be rewritten to adds whenever possible:
    check(i16(i8x) * 3 + i16(i8y) * 5, widening_mul(i8x, i8(3)) + widening_mul(i8y, i8(5)));
    check(i16(i8x) * 5 - i16(i8y) * 127, widening_mul(i8x, i8(5)) + widening_mul(i8y, i8(-127)));
    check(i16(u8x) * 3 - i16(u8y) * 7, widening_mul(u8x, i8(3)) + widening_mul(u8y, i8(-7)));
    check(i32(i8x) * 3 + i32(i8y) * 5, widening_add(widening_mul(i8x, i8(3)), widening_mul(i8y, i8(5))));
    check(i32(i8x) * 5 - i32(i8y) * 3, widening_add(widening_mul(i8x, i8(5)), widening_mul(i8y, i8(-3))));
    check(i32(u8x) * 7 - i32(u8y) * 6, widening_add(widening_mul(u8x, i8(7)), widening_mul(u8y, i8(-6))));

    check((i16(u8x) * 4 + i16(u8y)) * 3, widening_mul(u8x, i8(12)) + widening_mul(u8y, i8(3)));
    check((i16(i8x) + i16(i8y) * 7) * 5, widening_mul(i8x, i8(5)) + widening_mul(i8y, i8(35)));
    check((i16(u8x) * 4 - i16(u8y)) * 3, widening_mul(u8x, i8(12)) + widening_mul(u8y, i8(-3)));
    check((i16(i8x) - i16(i8y) * 7) * 5, widening_mul(i8x, i8(5)) + widening_mul(i8y, i8(-35)));

    check((u16(u8x) + u16(u8y)) * 2, widening_shift_left(u8x, u8(1)) + widening_shift_left(u8y, u8(1)));
    check((u16(u8x) - u16(u8y)) * 2, widening_shift_left(u8x, u8(1)) - widening_shift_left(u8y, u8(1)));
    check((u16(u8x) * 4 + u16(u8y)) * 3, widening_mul(u8x, u8(12)) + widening_mul(u8y, u8(3)));
    check((u16(u8x) + u16(u8y) * 7) * 5, widening_mul(u8x, u8(5)) + widening_mul(u8y, u8(35)));
    check((u16(u8x) * 4 - u16(u8y)) * 3, widening_mul(u8x, u8(12)) - widening_mul(u8y, u8(3)));
    check((u16(u8x) - u16(u8y) * 7) * 5, widening_mul(u8x, u8(5)) - widening_mul(u8y, u8(35)));

    printf("Success!\n");
    return 0;
}
