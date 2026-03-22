#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

bool check_lossless_cast(const Type &t, const Expr &in, const Expr &correct) {
    Expr result = lossless_cast(t, in);
    if (!equal(result, correct)) {
        std::cout << "Incorrect lossless_cast result:\n"
                  << "lossless_cast(" << t << ", " << in << ") gave:\n"
                  << " " << result
                  << " but expected was:\n"
                  << " " << correct << "\n";
        return true;
    }
    return false;
}

int main(int argc, char **argv) {
    Expr x = Variable::make(Int(32), "x");
    Type u8 = UInt(8);
    Type u16 = UInt(16);
    Type u32 = UInt(32);
    // Type u64 = UInt(64);
    Type i8 = Int(8);
    Type i16 = Int(16);
    Type i32 = Int(32);
    Type i64 = Int(64);
    Type u8x = UInt(8, 4);
    Type u16x = UInt(16, 4);
    Type u32x = UInt(32, 4);
    Expr var_u8 = Variable::make(u8, "x");
    Expr var_u16 = Variable::make(u16, "x");
    Expr var_u8x = Variable::make(u8x, "x");

    bool found_error = false;

    Expr e = cast(u8, x);
    found_error |= check_lossless_cast(i32, e, cast(i32, e));

    e = cast(u8, x);
    found_error |= check_lossless_cast(i32, e, cast(i32, e));

    e = cast(i8, var_u16);
    found_error |= check_lossless_cast(u16, e, Expr());

    e = cast(i16, var_u16);
    found_error |= check_lossless_cast(u16, e, Expr());

    e = cast(u32, var_u8);
    found_error |= check_lossless_cast(u16, e, cast(u16, var_u8));

    e = VectorReduce::make(VectorReduce::Add, cast(u16x, var_u8x), 1);
    found_error |= check_lossless_cast(u16, e, cast(u16, e));

    e = VectorReduce::make(VectorReduce::Add, cast(u32x, var_u8x), 1);
    found_error |= check_lossless_cast(u16, e, VectorReduce::make(VectorReduce::Add, cast(u16x, var_u8x), 1));

    e = cast(u32, var_u8) - 16;
    found_error |= check_lossless_cast(u16, e, Expr());

    e = cast(u32, var_u8) + 16;
    found_error |= check_lossless_cast(u16, e, cast(u16, var_u8) + 16);

    e = 16 - cast(u32, var_u8);
    found_error |= check_lossless_cast(u16, e, Expr());

    e = 16 + cast(u32, var_u8);
    found_error |= check_lossless_cast(u16, e, 16 + cast(u16, var_u8));

    // Check one where the target type is unsigned but there's a signed addition
    // (that can't overflow)
    e = cast(i64, cast(u16, var_u8) + cast(i32, 17));
    found_error |= check_lossless_cast(u32, e, cast(u32, cast(u16, var_u8)) + cast(u32, 17));

    // Check one where the target type is unsigned but there's a signed subtract
    // (that can overflow). It's not safe to enter the i16 sub
    e = cast(i64, cast(i16, 10) - cast(i16, 17));
    found_error |= check_lossless_cast(u32, e, Expr());

    e = cast(i64, 1024) * cast(i64, 1024) * cast(i64, 1024);
    found_error |= check_lossless_cast(i32, e, (cast(i32, 1024) * 1024) * 1024);

    // Check narrowing a vector reduction of something narrowable to bool ...
    auto make_reduce = [&](Type t, VectorReduce::Operator op) {
        return VectorReduce::make(op,
                                  cast(t.with_lanes(4), Ramp::make(x, 1, 4) > 4), 2);
    };

    // It's OK to narrow it to 8-bit.
    e = make_reduce(UInt(16), VectorReduce::Add);
    found_error |= check_lossless_cast(UInt(8), e, make_reduce(UInt(8), VectorReduce::Add));

    // ... but we can't reduce it all the way to bool if the operator isn't
    // legal for bools (issue #9011)
    e = make_reduce(UInt(8), VectorReduce::Add);
    found_error |= check_lossless_cast(Bool(), e, Expr());

    // Min or Max, however, can just become And and Or
    e = make_reduce(UInt(8), VectorReduce::Min);
    found_error |= check_lossless_cast(Bool(), e, make_reduce(Bool(), VectorReduce::And));

    e = make_reduce(UInt(8), VectorReduce::Max);
    found_error |= check_lossless_cast(Bool(), e, make_reduce(Bool(), VectorReduce::Or));

    if (found_error) {
        return 1;
    }

    std::cout << "Success!\n";
    return 0;
}
