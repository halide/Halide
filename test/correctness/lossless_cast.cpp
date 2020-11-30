#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

int check_lossless_cast(const Type &t, const Expr &in, const Expr &correct) {
    Expr result = lossless_cast(t, in);
    if (!equal(result, correct)) {
        std::cout << "Incorrect lossless_cast result:\nlossless_cast("
                  << t << ", " << in << ") gave: " << result
                  << " but expected was: " << correct << "\n";
        return 1;
    }
    return 0;
}

int lossless_cast_test() {
    Expr x = Variable::make(Int(32), "x");
    Type u8 = UInt(8);
    Type u16 = UInt(16);
    Type u32 = UInt(32);
    Type i8 = Int(8);
    Type i16 = Int(16);
    Type i32 = Int(32);
    Type u8x = UInt(8, 4);
    Type u16x = UInt(16, 4);
    Type u32x = UInt(32, 4);
    Expr var_u8 = Variable::make(u8, "x");
    Expr var_u16 = Variable::make(u16, "x");
    Expr var_u8x = Variable::make(u8x, "x");

    int res = 0;

    Expr e = cast(u8, x);
    res |= check_lossless_cast(i32, e, cast(i32, e));

    e = cast(u8, x);
    res |= check_lossless_cast(i32, e, cast(i32, e));

    e = cast(i8, var_u16);
    res |= check_lossless_cast(u16, e, Expr());

    e = cast(i16, var_u16);
    res |= check_lossless_cast(u16, e, Expr());

    e = cast(u32, var_u8);
    res |= check_lossless_cast(u16, e, cast(u16, var_u8));

    e = VectorReduce::make(VectorReduce::Add, cast(u16x, var_u8x), 1);
    res |= check_lossless_cast(u16, e, cast(u16, e));

    e = VectorReduce::make(VectorReduce::Add, cast(u32x, var_u8x), 1);
    res |= check_lossless_cast(u16, e, VectorReduce::make(VectorReduce::Add, cast(u16x, var_u8x), 1));

    return res;
}

int main() {
    if (lossless_cast_test()) {
        printf("lossless_cast test failed!\n");
        return 1;
    }
    printf("Success!\n");
    return 0;
}
