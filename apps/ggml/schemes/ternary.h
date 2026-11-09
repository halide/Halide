#pragma once

// GGML's ternary formats (ggml-quants.c quantize_row_tq1_0_ref,
// quantize_row_tq2_0_ref, quantize_row_q2_0_ref), as Approximations: codes
// in {-1, 0, 1} times an fp16 scale per 256 values, stored as 2-bit fields
// (tq2_0) or as base-3 digits, five per byte (tq1_0); or per 64 values, as
// 2-bit fields (q2_0).
#include "common.h"
#include "kquants.h"

#include <limits>

namespace ggml {

// GGML's (int)lroundf(x) as its arm64 build computes it: fcvtas to an int64
// (to nearest, ties away from zero; saturating; NaN to 0), truncated to its
// low 32 bits.
inline Expr lround_int(const Expr &x) {
    const float lim = 0x1p63f;
    Expr r = round_half_away(x);
    Expr low = cast<int32_t>(cast<int64_t>(clamp(r, -lim, std::nextafter(lim, 0.0f))));
    return sf(select(is_nan(x), 0, r >= lim, -1, low));
}

// The ternary quantization: d = amax, the greatest |x| by GGML's scan
// (MAX(amax, fabsf(x)) from 0: a NaN takes over, and the next value replaces
// it), so order-free, the greatest |x| past the last NaN (NaN if that is the
// last value); code = lroundf(x * (1 / d)) (0 for d = 0), as an int8 (GGML's
// trit - 1, mod 256: only blocks of NaN or subnormal d, whose 1 / d is not
// finite, have codes past [-1, 1]). Values = codes * d.
struct TernaryQuant {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func x = in[0], last("last_nan"), am("amax"), d("scale"), q("codes");
        Var j, b;
        RDom r(0, QK_K);
        last(b, _) = maximum(select(is_nan(x(r, b, _)), r, -1));
        am(b, _) = maximum(sf(select(r > last(b, _), abs(x(r, b, _)), 0.0f)));
        d(b, _) = select(last(b, _) == QK_K - 1, Expr(std::numeric_limits<float>::quiet_NaN()), am(b, _));
        q(j, b, _) = cast<int8_t>(lround_int(sf(x(j, b, _) * inverse(d(b, _)))));
        return {q, d};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        return {dequant(e)};
    }
    ApproximationSignature signature() const {
        return codes_and_scale(-1, 1);
    }
    Func error_bound(const std::vector<Func> &, const std::vector<Func> &e) const {
        return step_bound(e, 0.5);
    }
};

// block_tq2_0 {u8 qs[64]; f16 d}: the codes + 1 as q2_K's 2-bit fields (the
// field 3, which GGML's quantizer never writes, decodes to 2).
inline Approximation tq2_0(const std::string &lay, bool indexed = false) {
    return Compose{BlockReshape{QK_K, indexed}, TernaryQuant{}, Parallel{{"codes", multiples(0, -1, 4)}, {"scale", fp16()}},
                   Parallel{{"codes", PlanarFieldPack{2, 32}}},
                   layout(lay, {{"qs", UInt(8), QK_K / 4}, {"d", Float(16)}}, {"qs", "d"})};
}

// block_tq1_0 {u8 qs[48]; u8 qh[4]; f16 d}: the codes + 1 as base-3 digits,
// five per byte: values 0..159 in qs[0..31] (byte m: values m + 32 n), values
// 160..239 in qs[32..47] (m + 16 n), values 240..255 four per byte in qh
// (m + 4 n, the fifth digit 0).
inline Approximation tq1_0(const std::string &lay, bool indexed = false) {
    return Compose{BlockReshape{QK_K, indexed}, TernaryQuant{}, Parallel{{"codes", multiples(0, -1, 3)}, {"scale", fp16()}},
                   Parallel{{"codes", Segments{{{"qs_lo", 160}, {"qs_hi", 80}, {"qh", 16}}}}},
                   Parallel{{"qs_lo", TritPack{32}}, {"qs_hi", TritPack{16}}, {"qh", TritPack{4, 4}}},
                   layout(lay, {{"qs_lo", UInt(8), 32}, {"qs_hi", UInt(8), 16}, {"qh", UInt(8), QK_K / 64}, {"d", Float(16)}},
                          {"qs_lo", "qs_hi", "qh", "d"})};
}

// q2_0's quantization: d = amax, the greatest |x| by GGML's strict scan
// (fabsf(x) > amax from 0: NaNs never taken), so order-free; code =
// (int)roundf(x * (1 / d)) (0 for d = 0), clamped to [-1, 2] (GGML clamps
// the code + 1 to [0, 3]; arm64's conversion saturates, NaN to 0; x86-64's
// gives INT_MIN for inf and NaN alike, the code -1). Only blocks of
// d < 2^-128, whose 1 / d is inf, have a non-finite x / d: the code 2 (x > 0)
// on arm64, -1 on x86-64, with an error up to 2 d. Values = codes * d.
struct Q2Quant {
    static constexpr int block = 64;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func x = in[0], d("scale"), q("codes");
        Var j, b;
        RDom r(0, block);
        Expr xr = x(r, b, _);
        d(b, _) = maximum(sf(select(is_nan(xr), 0.0f, abs(xr))));
        Expr v = sf(x(j, b, _) * inverse(d(b, _)));
        Expr c = clamp(round_half_away(v), -1.0f, 2.0f);
        q(j, b, _) = cast<int8_t>(sf(select(target_arch_is(Target::X86), select(is_finite(v), c, -1.0f), select(is_nan(v), 0.0f, c))));
        return {q, d};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        return {dequant(e)};
    }
    ApproximationSignature signature() const {
        return codes_and_scale(-1, 2);
    }
    Func error_bound(const std::vector<Func> &, const std::vector<Func> &e) const {
        Func f("bound");
        Var j, b;
        Expr d = e[1](b, _);
        f(j, b, _) = abs(cast<double>(d)) * select(is_inf(sf(inverse(d))), Expr(2.0001), Expr(0.50005));
        return f;
    }
};

// block_q2_0 {f16 d; u8 qs[16]}: the codes + 1 as 2-bit fields, four per
// byte, least significant first.
inline Approximation q2_0(const std::string &lay, bool indexed = false) {
    return Compose{BlockReshape{Q2Quant::block, indexed}, Q2Quant{}, Parallel{{"codes", multiples(0, -1, 4)}, {"scale", fp16()}},
                   Parallel{{"codes", PlanarFieldPack{2, 1}}},
                   layout(lay, {{"d", Float(16)}, {"qs", UInt(8), Q2Quant::block / 4}}, {"qs", "d"})};
}

}  // namespace ggml
