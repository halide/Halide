#include "common.h"

using namespace Halide;

Expr saturating_rounding_doubling_high_multiply(Expr a, Expr b) {
    Type t = a.type();
    Type wider = t.with_bits(t.bits() * 2);
    Expr a_wide = cast(wider, a);
    Expr b_wide = cast(wider, b);
    Expr ab_wide = a_wide * b_wide;
    Expr nudge = 1 << (t.bits() - 2);
    Expr result = (ab_wide + nudge) >> (t.bits() - 1);
    return cast(t, clamp(result, t.min(), t.max()));
}

Expr rounding_shift_right(Expr x, Expr shift) {
    Halide::Type t = x.type();
    Halide::Type t_unsigned = t.with_code(halide_type_uint);
    Halide::Expr ushift = cast(t_unsigned, shift);
    // Shift must satisfy 0 <= shift <= 31
    Expr mask = ((cast(x.type(), 1) << ushift) - 1);
    Expr remainder = x & mask;
    Expr threshold = (mask >> 1) + select(x < 0, 1, 0);
    return (x >> ushift) + select(remainder > threshold, 1, 0);
}

Expr multiply_quantized_multiplier(Expr x, Expr q, Expr shift) {
    return rounding_shift_right(saturating_rounding_doubling_high_multiply(x, q), shift);
}
