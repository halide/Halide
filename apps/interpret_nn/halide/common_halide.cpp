#include "common_halide.h"

#include <type_traits>

namespace interpret_nn {

Halide::Expr SaturatingRoundingDoublingHighMultiply(const Halide::Expr &a,
                                                    const Halide::Expr &b) {
    Halide::Type t = a.type();
    Halide::Type wider = t.with_bits(t.bits() * 2);
    Halide::Expr a_wide = cast(wider, a);
    Halide::Expr b_wide = cast(wider, b);
    Halide::Expr ab_wide = a_wide * b_wide;
    // In Halide, integer division rounds to negative infinity, so division by a
    // power of two is the same as a shift (unlike C).
    int nudge = 1 << (t.bits() - 2);
    Halide::Expr result = (ab_wide + nudge) >> (t.bits() - 1);
    return saturating_cast(t, result);
}

Halide::Expr RoundingDivideByPOT(const Halide::Expr &x,
                                 const Halide::Expr &exponent) {
    // Unsigned type the same size as x
    Halide::Type t = x.type();
    Halide::Type t_unsigned = t.with_code(halide_type_uint);
    Halide::Expr uexponent = cast(t_unsigned, exponent);
    // Exponent must satisfy 0 <= exponent <= 31
    // TODO: Maybe this should be an offset added to x prior to shifting.
    Halide::Expr mask = (cast(x.type(), 1) << uexponent) - 1;
    Halide::Expr remainder = x & mask;
    Halide::Expr threshold = (mask >> 1) + (x < 0);
    return (x >> uexponent) + (remainder > threshold);
}

// The tflite function of the same name performs a left shift.
Halide::Expr MultiplyByQuantizedMultiplierSmallerThanOne(const Halide::Expr &x,
                                                         const Halide::Expr &q,
                                                         const Halide::Expr &shift) {
    return RoundingDivideByPOT(SaturatingRoundingDoublingHighMultiply(x, q),
                               shift);
}

}  // namespace interpret_nn
