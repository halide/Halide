#pragma once

// GGML's ternary formats (ggml-quants.c quantize_row_tq1_0_ref,
// quantize_row_tq2_0_ref), as Approximations: codes in {-1, 0, 1} times an
// fp16 scale per 256 values, stored as 2-bit fields (tq2_0) or as base-3
// digits, five per byte (tq1_0).
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
                   Parallel{{"codes", two_bit_planes()}},
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

}  // namespace ggml
