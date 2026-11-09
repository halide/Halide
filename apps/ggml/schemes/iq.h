#pragma once

// GGML's non-linear 4-bit formats (ggml-quants.c quantize_iq4_nl,
// quantize_iq4_xs, as ggml_quantize_chunk runs them: no importance matrix),
// as Approximations: codes drawn from kvalues_iq4nl (a codebook) times a
// scale per block of 32 (iq4_nl: fp16), or per sub-block of 32, a 6-bit
// multiple of a super-block's fp16 scale (iq4_xs). The quantizer is GGML's
// search, step for step (quantize_row_iq4_nl_impl), each float operation as
// GGML's arm64 build computes it (scratch/w5/exact).
#include "common.h"
#include "kquants.h"

namespace ggml {

inline const std::vector<int> &kvalues_iq4nl() {
    static const std::vector<int> v{-127, -104, -83, -65, -49, -35, -22, -10, 1, 13, 25, 38, 53, 69, 89, 113};
    return v;
}

// GGML's best_index_int8(n, values, x): the index of the value of the sorted
// table nearest x (in float; ties to the greater), 0 at or below the first
// value, n - 1 at or above the last (and for NaN). Its binary search as a
// count: mu, the first value above x, is the number of values (but the last)
// not above it.
inline Expr best_index_int8(const std::vector<int> &values, const Expr &x) {
    const int n = (int)values.size();
    std::vector<Expr> table;
    Expr mu = 0;
    for (int k = 0; k < n; k++) {
        table.push_back((float)values[k]);
        if (k < n - 1) mu += select(x < (float)values[k], 0, 1);
    }
    Expr nearest = select(x - mux(mu - 1, table) < mux(mu, table) - x, mu - 1, mu);
    return strict_float(select(x <= (float)values[0], 0, x >= (float)values[n - 1], n - 1, nearest));
}

// The kvalues_iq4nl codes nearest x (best_index_int8's).
inline Expr iq4nl_code(const Expr &x) {
    std::vector<Expr> kv;
    for (int v : kvalues_iq4nl()) {
        kv.push_back(cast<int8_t>(v));
    }
    return mux(best_index_int8(kvalues_iq4nl(), x), kv);
}

// quantize_row_iq4_nl_impl(super, 32, ..., kvalues_iq4nl, NULL, ntry = 7).
// Per sub-block of 32: weights x^2; (amax, max) by GGML's strict scan; for
// d = max / 127, then each of 15 trial iscales (itry - 127) / max, itry =
// -7..7, codes best_index_int8(iscale * x) and their least-squares scale
// sumqx / sumq2 (each sum fused, w * q rounded first), kept if its
// sumqx^2 / sumq2 beats the best so far; scale 0 for sub-blocks whose amax is
// below 1e-15. A lone block (iq4_nl): its scale, and the codes again at
// 1 / scale. Super-blocks (iq4_xs): the first scale of the greatest
// magnitude, max, d = -max / 32 (stored fp16), each sub-block's scale l =
// nearest_int(scale / d) in [-32, 31] (an int8), and the codes again at
// 1 / (d * l) (d in f32). Values = codes * scale (iq4_xs: codes * (d * l)).
struct IQ4Quant : ScaledCodes {
    static constexpr int trials = 16;
    int super;
    IQ4Quant(int super)
        : ScaledCodes{QK, -127, 113, -32, 31}, super(super) {
    }

    int sub() const {
        return super / n;
    }

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func x = in[0], am("sub_amax"), cand("iq4_trials"), st("iq4_search"), sc("sub_scales");
        Func mas("max_abs_scale"), first("first_max_scale"), d("scale"), six("scales"), q("codes");
        Var j, s, c, b, k;
        RDom r(0, n), rk(1, trials - 1), rs(0, sub());
        Expr xr = x(s * n + r, b, _);
        strict_amax(am, x, xr, s, b);
        Expr mx = am(s, b, _)[1];
        // trial c's iscale: c = 0 the first, 1 / (max / 127); then itry = c - 8
        Expr id = select(c == 0, sf(1.0f / sf(mx / 127.0f)), sf(cast<float>(c - 135) / mx));
        Expr fq = cast<float>(iq4nl_code(sf(id * xr))), wq = sf(sf(xr * xr) * fq);
        cand(rows_of(x, {c, s, b})) = Tuple(0.0f, 0.0f);
        cand(c, s, b, _) = Tuple(madd(wq, xr, cand(c, s, b, _)[0]), madd(wq, fq, cand(c, s, b, _)[1]));
        // (scale, best)
        Expr qx0 = cand(0, s, b, _)[0], q20 = cand(0, s, b, _)[1], d0 = sf(select(q20 > 0.0f, qx0 / q20, 0.0f));
        st(s, b, _) = Tuple(d0, sf(d0 * qx0));
        Expr qx = cand(rk, s, b, _)[0], q2 = cand(rk, s, b, _)[1];
        Expr better = sf(q2 > 0.0f && qx * qx > st(s, b, _)[1] * q2), fit = sf(qx / q2);
        st(s, b, _) = Tuple(select(better, fit, st(s, b, _)[0]), select(better, sf(fit * qx), st(s, b, _)[1]));
        sc(s, b, _) = select(sf(am(s, b, _)[0] < 1e-15f), 0.0f, st(s, b, _)[0]);
        if (sub() == 1) {
            d(b, _) = sc(0, b, _);
            q(j, b, _) = iq4nl_code(sf(inverse(d(b, _)) * x(j, b, _)));
            return {q, d};
        }
        Expr ms = first_max_scale(mas, first, sc, b, rs, sub());
        ms = select(sf(mas(b, _) > 0.0f), ms, 0.0f);
        d(b, _) = sf(-ms / 32.0f);
        six(k, b, _) = cast<int8_t>(clamp(nearest_int(sf(inverse(d(b, _)) * sc(k, b, _))), -32, 31));
        Expr dl = sf(d(b, _) * cast<float>(six(j / n, b, _)));
        q(j, b, _) = iq4nl_code(sf(inverse(dl) * x(j, b, _)));
        return {q, six, d};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        if (sub() == 1) return {dequant(e)};
        return ScaledCodes::decode(e);
    }
    ApproximationSignature signature() const {
        if (sub() == 1) return codes_and_scale(lo, hi);
        return ScaledCodes::signature();
    }
    // Codes nearest x / d1 among kvalues_iq4nl, at most 24 apart.
    Func error_bound(const std::vector<Func> &in, const std::vector<Func> &e) const {
        return nearest_bound(in, e, 12.0);
    }
};

// The codes <-> their indices in kvalues_iq4nl (decoded by a table lookup,
// through a lut: as GGML's NEON vqtbl1q_s8, or Metal's threadgroup copy;
// see codebook_lut()).
inline Approximation iq4nl_codes() {
    return codebook_lut("iq4nl", kvalues_iq4nl());
}

// block_iq4_nl {f16 d; u8 qs[16]}: the codes' indices packed as q4_0's
// (code j in the low nibble of byte j, code j + 16 in its high nibble).
inline Approximation iq4_nl(const std::string &lay, bool indexed = false) {
    return Compose{BlockReshape{QK, indexed}, IQ4Quant{QK}, Parallel{{"codes", iq4nl_codes()}, {"scale", fp16()}},
                   Parallel{{"codes", PlanarFieldPack{4, QK / 2}}},
                   layout(lay, {{"d", Float(16)}, {"qs", UInt(8), QK / 2}}, {"qs", "d"})};
}

// block_iq4_xs {f16 d; u16 scales_h; u8 scales_l[4]; u8 qs[128]}: per 32
// codes, 16 bytes of their indices packed as iq4_nl's; the scales + 32, their
// low nibbles in scales_l (scale i in nibble i % 2 of byte i / 2), their top
// two bits in scales_h (bits 2i).
inline Approximation iq4_xs(const std::string &lay, bool indexed = false) {
    return Compose{BlockReshape{QK_K, indexed}, IQ4Quant{QK_K}, Parallel{{"codes", iq4nl_codes()}},
                   Parallel{{"codes", PlanarFieldPack{4, QK / 2}},
                            {"scales", six_bit_scales(PlanarFieldPack{4, 1}, PlanarFieldPack{2, 1})},
                            {"scale", fp16()}},
                   layout(lay, {{"d", Float(16)}, {"scales_h", UInt(8), 2}, {"scales_l", UInt(8), QK_K / 64}, {"qs", UInt(8), QK_K / 2}},
                          {"qs", "scales_l", "scales_h", "d"})};
}

}  // namespace ggml
