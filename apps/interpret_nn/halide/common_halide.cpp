#include "common_halide.h"

using namespace Halide;

namespace interpret_nn {

Expr multiply_2x_high(const Expr &a, const Expr &b) {
    Type t = a.type();
    Type wider = t.with_bits(t.bits() * 2);
    Expr a_wide = cast(wider, a);
    Expr b_wide = cast(wider, b);
    Expr ab_wide = a_wide * b_wide;
    // In Halide, integer division rounds to negative infinity, so division by a
    // power of two is the same as a shift (unlike C).
    int nudge = 1 << (t.bits() - 2);
    Expr result = (ab_wide + nudge) >> (t.bits() - 1);
    return saturating_cast(t, result);
}

Expr round_shift_right(const Expr &x, const Expr &exponent) {
    // Unsigned type the same size as x
    Type t = x.type();
    Type t_unsigned = t.with_code(halide_type_uint);
    Expr uexponent = cast(t_unsigned, exponent);
    // Exponent must satisfy 0 <= exponent <= 31
    // TODO: Maybe this should be an offset added to x prior to shifting.
    Expr mask = (cast(x.type(), 1) << uexponent) - 1;
    Expr remainder = x & mask;
    Expr threshold = (mask >> 1) + (x < 0);
    return (x >> uexponent) + (remainder > threshold);
}

// The tflite function of the same name performs a left shift.
Expr multiply_quantized(const Expr &x, const Expr &q, const Expr &shift) {
    return round_shift_right(multiply_2x_high(x, q), shift);
}

}  // namespace interpret_nn
