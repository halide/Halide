#include "Halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;
using namespace Halide::Internal;

#define internal_assert _halide_user_assert

void check(Expr test, Expr expected) {
    Expr result = pattern_match_intrinsics(test);
    if (!equal(result, expected)) {
        std::cout << "failure!\n";
        std::cout << "test: " << test << "\n";
        std::cout << "result: " << result << "\n";
        std::cout << "exepcted: " << expected << "\n";
        abort();
    }
}

int main(int argc, char **argv) {
    Expr xi = Variable::make(Int(8, 4), "xi");
    Expr yi = Variable::make(Int(8, 4), "yi");
    Expr xu = Variable::make(UInt(8, 4), "xu");
    Expr yu = Variable::make(UInt(8, 4), "yu");

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

    check(i8_sat(i16(xi) + yi), saturating_add(xi, yi));
    check(u8_sat(u16(xu) + yu), saturating_add(xu, yu));

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

    printf("Success!\n");
    return 0;
}
