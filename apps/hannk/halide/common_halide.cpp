#include "common_halide.h"

using namespace Halide;

namespace hannk {

void interpret_as_tensor(OutputImageParam p) {
    p.dim(0).set_stride(1).set_min(0);
}

void require_same_min_extent(int first_dim, OutputImageParam first, int second_dim, OutputImageParam second) {
    second.dim(second_dim).set_min(first.dim(first_dim).min());
    second.dim(second_dim).set_extent(first.dim(first_dim).extent());
}

void require_same_min_extent(int d, OutputImageParam first, OutputImageParam second) {
    second.dim(d).set_min(first.dim(d).min());
    second.dim(d).set_extent(first.dim(d).extent());
}

void require_same_extent_cx(OutputImageParam first, OutputImageParam second) {
    for (int d = 0; d < 2; d++) {
        require_same_min_extent(d, first, second);
    }
}

Expr can_fuse_cx(OutputImageParam p) {
    return
        p.dim(0).min() == 0 &&
        p.dim(1).stride() > 0 &&
        p.dim(1).stride() == p.dim(0).extent() * p.dim(0).stride();
}

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
    Expr bounded = t(clamp(c, min_c, min_c + extent_c - 1),
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
    // Exponent must satisfy 0 <= exponent <= 31
    Type t = a.type();
    Expr ab_wide = widening_mul(a, b);
    // In Halide, integer division rounds to negative infinity, so division by a
    // power of two is the same as a shift (unlike C).
    // TODO: Using rounding_shift_right here doesn't generate qrdmulh :(
    int nudge = 1 << (t.bits() - 2);
    Expr result = (ab_wide + nudge) >> (t.bits() - 1);
    return saturating_cast(t, result);
}

Expr multiply_quantized(const Expr &x, const Expr &q, const Expr &shift) {
    return rounding_shift_right(multiply_2x_high(x, q), shift);
}

}  // namespace hannk
