#include "common.h"

using namespace Halide;

void RequireAligned(const int alignment, OutputImageParam* param) {
    // The first dimension should have a min/extent aligned to the required
    // alignment, we assume the stride is 1.
    param->dim(0).set_min((param->dim(0).min() / alignment) * alignment);
    param->dim(0).set_extent((param->dim(0).extent() / alignment) * alignment);

    // The rest of the dimensions should have a stride aligned to the required
    // alignment.
    for (int i = 1; i < param->dimensions(); i++) {
        param->dim(i).set_stride((param->dim(i).stride() / alignment) * alignment);
    }
}

Expr SaturatingRoundingDoublingHighMultiply(Expr a, Expr b) {
    Type t = a.type();
    Type wider = t.with_bits(t.bits() * 2);
    Expr a_wide = cast(wider, a);
    Expr b_wide = cast(wider, b);
    Expr ab_wide = a_wide * b_wide;
    Expr nudge = 1 << (t.bits() - 2);
    Expr result = (ab_wide + nudge) >> (t.bits() - 1);
    return cast(t, clamp(result, t.min(), t.max()));
}

Expr RoundingShiftRight(Expr x, Expr shift) {
    // Shift must satisfy 0 <= shift <= 31
    Expr mask = ((1ll << shift) - 1);
    Expr remainder = x & mask;
    Expr threshold = (mask >> 1) + select(x < 0, 1, 0);
    return (x >> shift) + select(remainder > threshold, 1, 0);
}

Expr MultiplyByQuantizedMultiplier(Expr x, Expr q, Expr shift) {
    return RoundingShiftRight(SaturatingRoundingDoublingHighMultiply(x, q), shift);
}
