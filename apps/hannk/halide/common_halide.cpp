#include "common_halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace hannk {

int get_register_count(const Target &target) {
    switch (target.arch) {
    case Target::X86:
        return target.has_feature(Target::AVX512_Skylake) ? 32 : 16;
    case Target::ARM:
        return target.bits == 64 ? 32 : 16;
    case Target::Hexagon:
        return 32;
    default:
        return 16;
    }
}

void interpret_as_tensor(OutputImageParam p) {
    p.dim(0).set_stride(1).set_min(0);
}

void require_same_min_extent(int d, OutputImageParam first, OutputImageParam second) {
    second.dim(d).set_min(first.dim(d).min());
    second.dim(d).set_extent(first.dim(d).extent());
}

Expr is_interleaved(OutputImageParam p, int channels) {
    return p.dim(0).min() == 0 &&
           p.dim(0).extent() == channels &&
           p.dim(1).stride() == channels;
}

Expr align_down(const Expr &x, const Expr &n) {
    return (x / n) * n;
}

Expr align_up(const Expr &x, const Expr &n) {
    return ((x + n - 1) / n) * n;
}

Expr align(const Expr &x, const Expr &n) {
    return align_down(x, n);
}

Expr multiply_2x_high(const Expr &a, const Expr &b) {
    Type t = a.type().bits() > b.type().bits() ? a.type() : b.type();
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

Expr floor_log2(const Expr &x) {
    //   floor(log2(x)) = B - clz(x) => log2(x) ~ B - clz(x)
    //   B = sizeof(x)*8 - 1
    //   clz(x) = count_leading_zeros(x)
    int log2_max_x = x.type().bits() - 1;
    return log2_max_x - i16(count_leading_zeros(x));
}

Expr approx_log2(const Type &type, const Expr &x, int log2_precision) {
    int precision = 1 << log2_precision;

    Expr floor_log2_x = floor_log2(x);

    // Use a cubic polynomial to interpolate the fractional part of the result.
    // TODO: A cubic might be overkill for our needs.
    // Coefficients produced by the following numpy snippet:
    //
    //   points = 6
    //   poly_x = np.arange(points, 2 * points + 1) / points
    //   poly_y = np.log2(poly_x)
    //   p = np.polyfit(poly_x - 1, poly_y, 3)
    const int p3 = std::lround(1.55971251e-01 * (1 << 15));
    const int p2 = std::lround(-5.75039427e-01 * (1 << 15));
    const int p1 = std::lround(0.41903642e+00 * (1 << 15));
    const int p0 = std::lround(3.32891346e-04 * (1 << 15));

    Expr frac1 = i16(x << (15 - floor_log2_x)) & 0x7fff;
    Expr frac2 = multiply_2x_high(frac1, frac1);
    Expr frac3 = multiply_2x_high(frac2, frac1);

    Expr poly =
        i32(multiply_2x_high(i16(p3), frac3) + multiply_2x_high(i16(p2), frac2) + p0) +
        i32(multiply_2x_high(i16(p1), frac1)) + i32(frac1);

    Expr result = saturating_add(
        cast(type, floor_log2_x) << log2_precision,
        cast(type, rounding_shift_right(poly, 15 - log2_precision)));

    // For x <= 0, return any negative value. If count_leading_zeros returns
    // x.type().bits(), which appears to be the case on every platform we
    // target, both sides of this select are the same.
    return select(x > 0, result, -precision);
}

Expr approx_log2(const Expr &x, int log2_precision) {
    return approx_log2(Int(32), x, log2_precision);
}

Expr approx_exp2(const Type &type, const Expr &x, const Expr &log2_precision_x, int log2_precision_result) {
    // Compute floor(x / precision_x) and frac(x / precision_x)
    Expr floor_x = cast(type, x >> log2_precision_x);

    Expr exp2_floor_x = cast(type, 1) << (floor_x + log2_precision_result);

    // Use a cubic polynomial to interpolate the fractional part of the argument.
    // TODO: A cubic might be overkill for our needs.
    // Coefficients produced by the following numpy snippet:
    //
    //   points = 6
    //   poly_x = np.arange(points, 2 * points + 1) / points
    //   poly_y = np.exp2(poly_x - 1) - 1
    //   p = np.polyfit(poly_x - 1, poly_y, 3)
    //
    // We ignore the constant term from the polynomial.
    const int p3 = std::lround(7.91076597e-02 * (1 << 15));
    const int p2 = std::lround(2.24701130e-01 * (1 << 15));
    const int p1 = std::lround(6.96189819e-01 * (1 << 15)) - 1;  // Hack to avoid overflow below.

    Expr frac1 = i16(x - (floor_x << log2_precision_x)) << (15 - i16(log2_precision_x));
    Expr frac2 = multiply_2x_high(frac1, frac1);
    Expr frac3 = multiply_2x_high(frac2, frac1);

    assert(p1 + p2 + p3 < (1 << 15));
    Expr poly =
        multiply_2x_high(i16(p3), frac3) +
        multiply_2x_high(i16(p2), frac2) +
        multiply_2x_high(i16(p1), frac1);
    poly = cast(type, poly) << (type.bits() - 16);

    return saturating_add(exp2_floor_x, multiply_2x_high(exp2_floor_x, poly));
}

Expr approx_exp2(const Expr &x, const Expr &log2_precision_x, int log2_precision_result) {
    return approx_exp2(Int(32), x, log2_precision_x, log2_precision_result);
}

Expr approx_reciprocal_sqrt(const Expr &x, int log2_precision) {
    //   precision / sqrt(x)
    // = precision / 2^log2(x^(1/2))
    // = precision * 2^(-log2(x)/2)
    Expr log2_x = approx_log2(x, 14);
    return approx_exp2(-log2_x, 15, log2_precision);
}

}  // namespace hannk
