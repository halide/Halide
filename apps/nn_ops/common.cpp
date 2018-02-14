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
    // Shift must satisfy 0 <= shift <= 31
    Expr mask = ((1ll << shift) - 1);
    Expr remainder = x & mask;
    Expr threshold = (mask >> 1) + select(x < 0, 1, 0);
    return (x >> shift) + select(remainder > threshold, 1, 0);
}

Expr multiply_quantized_multiplier(Expr x, Expr q, Expr shift) {
    return rounding_shift_right(saturating_rounding_doubling_high_multiply(x, q), shift);
}
