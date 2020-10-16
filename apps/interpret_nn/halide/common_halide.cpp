#include "common_halide.h"

#include <type_traits>

namespace interpret_nn {

Halide::Expr SaturatingRoundingDoublingHighMultiply(const Halide::Expr &a,
                                                    const Halide::Expr &b) {
    Halide::Type t = a.type();
    Halide::Type wider = t.with_bits(t.bits() * 2);
    Halide::Type wider_u = wider.with_code(halide_type_uint);
    Halide::Expr a_wide = cast(wider, a);
    Halide::Expr b_wide = cast(wider, b);
    Halide::Expr ab_wide = a_wide * b_wide;
    // In Halide, integer division rounds to negative infinity, so division by a
    // power of two is the same as a shift (unlike C).
    Halide::Expr nudge = 1 << (t.bits() - 2);
    Halide::Expr result = (ab_wide + nudge) >> cast(wider_u, t.bits() - 1);
    return cast(t, clamp(result, t.min(), t.max()));
}

Halide::Expr SaturatingRoundingMultiplyByPOT(const Halide::Expr &x,
                                             const Halide::Expr &exponent) {
    Halide::Type t = x.type();
    Halide::Type t_unsigned = t.with_code(halide_type_uint);
    Halide::Expr uexponent = cast(t_unsigned, exponent);
    Halide::Expr threshold = (1 << cast(t_unsigned, t.bits() - 1 - exponent)) - 1;
    return select(x > threshold, t.max(),
                  select(x < -threshold, t.min(), x << uexponent));
}

Halide::Expr RoundingDivideByPOT(const Halide::Expr &x,
                                 const Halide::Expr &exponent) {
    // Unsigned type the same size as x
    Halide::Type t = x.type();
    Halide::Type t_unsigned = t.with_code(halide_type_uint);
    Halide::Expr uexponent = cast(t_unsigned, exponent);
    // Exponent must satisfy 0 <= exponent <= 31
    // TODO: Maybe this should be an offset added to x prior to shifting.
    Halide::Expr mask = ((cast(x.type(), 1) << uexponent) - 1);
    Halide::Expr remainder = x & mask;
    Halide::Expr threshold = (mask >> 1) + select(x < 0, 1, 0);
    return (x >> uexponent) + select(remainder > threshold, 1, 0);
}

// The tflite function of the same name performs a left shift.
Halide::Expr MultiplyByQuantizedMultiplierSmallerThanOne(const Halide::Expr &x,
                                                         const Halide::Expr &q,
                                                         const Halide::Expr &shift) {
    return RoundingDivideByPOT(SaturatingRoundingDoublingHighMultiply(x, q),
                               shift);
}

Halide::Expr MultiplyByQuantizedMultiplierGreaterThanOne(
    const Halide::Expr &input, const Halide::Expr &quantized_multiplier,
    const Halide::Expr &left_shift) {
    Halide::Type t = input.type().with_code(halide_type_uint);
    return SaturatingRoundingDoublingHighMultiply(
        input * (1 << cast(t, left_shift)), quantized_multiplier);
}

}  // namespace interpret_nn
