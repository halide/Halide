#include "common_halide.h"

using namespace Halide;

namespace interpret_nn {

// A tensor has the same requirements as a buffer in Halide by default, except
// the min of the innermost dimension must also be 0.
void interpret_as_tensor(OutputImageParam p) {
    p.dim(0).set_stride(1).set_min(0);
}

// Require that the first two dimensions of two buffers have the same bounds.
void require_same_extent_cx(OutputImageParam first, OutputImageParam second) {
    for (int d = 0; d < 2; d++) {
        second.dim(d).set_min(first.dim(d).min());
        second.dim(d).set_extent(first.dim(d).extent());
    }
}

// Check if the first two dimensions of a buffer can be fused cleanly.
Expr can_fuse_cx(OutputImageParam p) {
    return p.dim(0).min() == 0 && p.dim(1).stride() > 0 && p.dim(1).stride() == p.dim(0).extent();
}

// A boundary condition, without likelies that cause loop partitioning.
Func constant_exterior_tensor(
    Func t, Expr exterior,
    Expr min_c, Expr extent_c,
    Expr min_x, Expr extent_x,
    Expr min_y, Expr extent_y,
    Expr min_b, Expr extent_b) {
    Var c("c"), x("x"), y("y"), b("b");
    // We usually don't care about what comes after the boundary in the c
    // or b dimensions, so just skip those for the select.
    Expr in_bounds =
        min_x <= x && x < min_x + extent_x &&
        min_y <= y && y < min_y + extent_y;
    Expr bounded("bounded");
    bounded = t(clamp(c, min_c, min_c + extent_c - 1),
                clamp(x, min_x, min_x + extent_x - 1),
                clamp(y, min_y, min_y + extent_y - 1),
                clamp(b, min_b, min_b + extent_b - 1));

    Func tensor_bounded("tensor_bounded");
    tensor_bounded(c, x, y, b) = select(in_bounds, bounded, exterior);

    return tensor_bounded;
}

Func constant_exterior_tensor(ImageParam p, Expr exterior) {
    return constant_exterior_tensor(p, exterior,
                                    p.dim(0).min(), p.dim(0).extent(),
                                    p.dim(1).min(), p.dim(1).extent(),
                                    p.dim(2).min(), p.dim(2).extent(),
                                    p.dim(3).min(), p.dim(3).extent());
}

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
