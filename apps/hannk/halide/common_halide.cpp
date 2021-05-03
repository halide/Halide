#include "halide/common_halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace hannk {

int get_register_count(const Target &target) {
    switch (target.arch) {
    case Target::X86:
        return target.features_any_of({Target::AVX512_Skylake, Target::AVX512_Cannonlake, Target::AVX512_SapphireRapids}) ? 32 : 16;
    case Target::ARM:
        return target.bits == 64 ? 32 : 16;
    case Target::Hexagon:
        return 32;
    default:
        return 16;
    }
}

int get_vector_reduction_factor(const Target &target, Type t) {
    if (target.arch == Target::Hexagon ||
        target.has_feature(Target::ARMDotProd) ||
        target.has_feature(Target::AVX512_SapphireRapids)) {
        return 32 / t.bits();
    }

    // Most targets can do 2-way horizontal reductions well.
    return 2;
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
    return rounding_mul_shift_right(a, b, std::max(a.type().bits(), b.type().bits()) - 1);
}

Expr floor_log2(const Expr &x) {
    //   floor(log2(x)) = B - clz(x) => log2(x) ~ B - clz(x)
    //   B = sizeof(x)*8 - 1
    //   clz(x) = count_leading_zeros(x)
    int log2_max_x = x.type().bits() - 1;
    return log2_max_x - i16(count_leading_zeros(x));
}

Expr approx_log2(int q, const Expr &x, int q_x, const Type &type) {
    Expr floor_log2_x = floor_log2(x);

    // Use a cubic polynomial to interpolate the fractional part of the result.
    // TODO: A cubic might be overkill for our needs.
    // Coefficients produced by the following numpy snippet:
    //
    //   points = 6
    //   poly_x = np.arange(points, 2 * points + 1) / points
    //   poly_y = np.log2(poly_x)
    //   p = np.polyfit(poly_x - 1, poly_y, 3)
    //
    // Quantize to 14 bits so the polynomial evaluation fits in 15 bits.
    const int poly_bits = 14;
    const int p3 = std::lround(1.55971251e-01 * (1 << poly_bits));
    const int p2 = std::lround(-5.75039427e-01 * (1 << poly_bits));
    const int p1 = std::lround(1.41903642e+00 * (1 << poly_bits));
    const int p0 = std::lround(3.32891346e-04 * (1 << poly_bits));

    Expr frac1 = i16(x << (15 - floor_log2_x)) & 0x7fff;
    Expr frac2 = multiply_2x_high(frac1, frac1);
    Expr frac3 = multiply_2x_high(frac2, frac1);

    // TODO: On ARM, these polynomial coefficients each get broadcasted into their
    // own register. But, we could be using the "lane" version of the qrdmulh
    // instruction, and put all of the coefficients into one vector register. This
    // would reduce register pressure, which is very high in code using this helper.
    Expr poly =
        multiply_2x_high(i16(p3), frac3) +
        multiply_2x_high(i16(p2), frac2) +
        multiply_2x_high(i16(p1), frac1) +
        p0;
    Expr frac_result;
    if (q < poly_bits) {
        frac_result = cast(type, rounding_shift_right(poly, poly_bits - q));
    } else {
        frac_result = cast(type, poly) << (q - poly_bits);
    }

    // We've computed log2(x*2^q_x) = log2(x) + q_x. Subtract
    // that offset now, before we scale up the output.
    Expr floor_result = cast(type, floor_log2_x - q_x) << q;

    return saturating_add(floor_result, frac_result);
}

Expr approx_exp2(int q, const Expr &x, const Expr &q_x, const Type &type) {
    // Compute floor(x / precision_x) and frac(x / precision_x)
    Expr floor_x = cast(type, x >> q_x);

    Expr exp2_floor_x = saturating_cast(type, 1 << (floor_x + q));

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

    Expr frac1 = i16(x - (floor_x << q_x)) << (15 - i16(q_x));
    Expr frac2 = multiply_2x_high(frac1, frac1);
    Expr frac3 = multiply_2x_high(frac2, frac1);

    // TODO: On ARM, these polynomial coefficients each get broadcasted into their
    // own register. But, we could be using the "lane" version of the qrdmulh
    // instruction, and put all of the coefficients into one vector register. This
    // would reduce register pressure, which is very high in code using this helper.
    assert(p1 + p2 + p3 < (1 << 15));
    Expr poly =
        multiply_2x_high(i16(p3), frac3) +
        multiply_2x_high(i16(p2), frac2) +
        multiply_2x_high(i16(p1), frac1);
    poly = cast(type, poly) << (type.bits() - 16);

    return saturating_add(exp2_floor_x, multiply_2x_high(exp2_floor_x, poly));
}

Expr approx_reciprocal(int q, const Expr &x, const Type &type) {
    //   precision / x
    // = precision / 2^log2(x)
    // = precision * 2^(-log2(x))
    Expr log2_x = approx_log2(15, x, 0);
    return approx_exp2(q, -log2_x, 15, type);
}

Expr approx_reciprocal_sqrt(int q, const Expr &x, const Type &type) {
    //   precision / sqrt(x)
    // = precision / 2^log2(x^(1/2))
    // = precision * 2^(-log2(x)/2)
    Expr log2_x = approx_log2(14, x, 0);
    return approx_exp2(q, -log2_x, 15, type);
}

// TODO: These implementations are pretty slow, at least on x86. However:
// - They are readily implementable on every target
// - Produce identical results on every target
// - Avoid the use of lookup tables, which can be annoying on some targets
// - Negligibly impact overall performance in most realistic workloads

// Approximate log2(2^(x/2^q) +/- 1)*2^q
Expr approx_log2_exp2_plus_or_minus_one(int q, Expr x, int sign, Expr q_x, Type type) {
    // TODO: Try to make this intermediate fit in 16 bits.
    const int q_exp = 16;
    int one = sign << q_exp;
    Expr one_plus_exp2_x = one + approx_exp2(q_exp, x, q_x, Int(32));
    Expr raw = approx_log2(q, one_plus_exp2_x, q_exp, type);

    // For large x, the intermediate overflows. But log2(1 + 2^x) when x is large is just x.
    const int threshold = 30 - q_exp;
    Expr line = saturating_cast(type, rounding_shift_right(cast(type.widen(), x), i16(q_x) - q));
    return select((x >> q_x) < threshold, raw, line);
}

Expr approx_log2p1_exp2(int q, const Expr &x, const Expr &q_x, const Type &type) {
    return approx_log2_exp2_plus_or_minus_one(q, x, 1, q_x, type);
}

Expr approx_log2m1_exp2(int q, const Expr &x, const Expr &q_x, const Type &type) {
    return approx_log2_exp2_plus_or_minus_one(q, x, -1, q_x, type);
}

const float log2_e = 1.442695f;

Expr approx_logistic(int q, const Expr &x, const Expr &q_x, const Type &type) {
    // log2(e) is ~1.5, so to implement this, we quantize log2(e)/2, and adjust
    // q_x to compensate.
    const int log2_e_q = std::lround(log2_e * (1 << (x.type().bits() - 2)));
    Expr x2 = multiply_2x_high(x, cast(x.type(), -log2_e_q));

    const int log_q = 11;
    Expr log2_d = approx_log2p1_exp2(log_q, x2, q_x - 1, Int(16));
    return approx_exp2(q, -log2_d, log_q, type);
}

Expr approx_tanh(int q, const Expr &x, const Expr &q_x, const Type &type) {
    // log2(e) is ~1.5, so to implement this, we quantize log2(e)/2, and adjust
    // q_x to compensate.
    const int log2_e_q = std::lround(log2_e * (1 << (x.type().bits() - 2)));
    Expr x2 = multiply_2x_high(x, cast(x.type(), log2_e_q));

    const int log_q = 11;
    Expr log2_n = approx_log2m1_exp2(log_q, i16(abs(x2)), q_x - 2, Int(16));
    Expr log2_d = approx_log2p1_exp2(log_q, i16(abs(x2)), q_x - 2, Int(16));
    Expr abs_output = approx_exp2(q, log2_n - log2_d, log_q, type);
    return select(x2 < 0, -abs_output, x2 == 0, 0, abs_output);

    // TODO: Try approx_logistic(q + 1, x, q_x - 1, type) - (1 << q) instead, it
    // might be faster.
}

}  // namespace hannk
