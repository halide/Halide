// A collection of utility functions shared by the halide generators.

#ifndef COMMON_HALIDE_H_
#define COMMON_HALIDE_H_

#include <Halide.h>

using namespace Halide;

// This function implements the same computation as the ARMv7 NEON VQRDMULH
// instruction.
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

// Performs right shift and multiply by a multiplier.
Expr multiply_quantized_multiplier(Expr x, Expr q, Expr shift) {
    return rounding_shift_right(saturating_rounding_doubling_high_multiply(x, q), shift);
}

#endif