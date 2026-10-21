#pragma once

// GGML's K-quants (ggml-quants.c *_ref), as Approximations: super-blocks of
// QK_K = 256 values, in sub-blocks that each have their own scale (and min),
// themselves quantized against the super-block's fp16 scale(s); and q8_K,
// their activations. The quantizers are GGML's searches, step for step: each
// scan a sequential reduction (vectorized across sub-blocks), each float
// operation as GGML's arm64 build computes it (strict_float; fma where clang
// contracts a product into a sum, -ffp-contract=on, but not in the loops it
// vectorizes: there the products are rounded, then summed in order), so the
// bytes match (scratch/w4a/exact). GGML's quantizers are its reference ones,
// which ggml_quantize_chunk uses without an importance matrix.
#include "common.h"

namespace ggml {

constexpr int QK_K = 256;

// GGML's nearest_int: f + 1.5 * 2^23 rounds f (|f| < 2^22) to an integer, to
// nearest even, into the low mantissa bits.
inline Expr nearest_int(const Expr &f) {
    Expr v = strict_float(f + 12582912.0f);
    return (reinterpret<int32_t>(v) & 0x7fffff) - 0x400000;
}

inline Expr sf(const Expr &e) {
    return strict_float(e);
}

// The sums of each run of n codes, codes(j, b, ...) -> (j / n, b, ...), as
// int16 (block_q8_K's bsums, n = 16): a derived port.
inline Derive::Definition partial_sums(int n) {
    return [=](const std::vector<Func> &from) {
        Func s("partial_sums");
        Var t, b;
        RDom r(0, n);
        s(t, b, _) = cast<int16_t>(sum(cast<int>(from[0](t * n + r, b, _))));
        return s;
    };
}

// block_q8_K: max = the first value of the greatest |x| (GGML's strict scan,
// found order-free: the least index at the maximum), iscale = -127 / max,
// code = min(127, nearest_int(iscale * x)), d = 1 / iscale in f32 (0, codes
// 0, for all-zero blocks).
struct Q8KQuant {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func x = in[0], amax("amax"), first("first_max"), d("scale"), q("codes");
        Var j, b;
        RDom r(0, QK_K);
        amax(b, _) = maximum(sf(select(abs(x(r, b, _)) > 0.0f, abs(x(r, b, _)), 0.0f)));
        first(b, _) = minimum(select(abs(x(r, b, _)) == amax(b, _), r, QK_K - 1));
        Expr zero = sf(amax(b, _) == 0.0f), iscale = sf(-127.0f / x(clamp(first(b, _), 0, QK_K - 1), b, _));
        d(b, _) = select(zero, 0.0f, sf(1.0f / iscale));
        q(j, b, _) = select(zero, cast<int8_t>(0), cast<int8_t>(min(nearest_int(sf(iscale * x(j, b, _))), 127)));
        return {q, d};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        return {dequant(e)};
    }
    static ApproximationSignature signature() {
        return codes_and_scale(-127, 127);
    }
    Func error_bound(const std::vector<Func> &, const std::vector<Func> &e) const {
        return step_bound(e, 0.5);
    }
};

// block_q8_K {f32 d; int8 qs[256]; int16 bsums[16]}: bsums the sums of each
// 16 codes (a derived port).
inline Approximation q8_K(const std::string &lay, bool indexed = false) {
    return Compose{BlockReshape{QK_K, indexed}, Q8KQuant{}, Derive{"bsums", {"codes"}, partial_sums(16)},
                   layout(lay, {{"d", Float(32)}, {"qs", Int(8), QK_K}, {"bsums", Int(16), QK_K / 16}}, {"qs", "d", "bsums"})};
}

// q4_K's (and q5_K's) 6-bit scales and mins of 8 sub-blocks, v(k, b, ...)
// in [0, 63] (k < 8 the scales, the rest the mins) <-> GGML's 12 bytes
// (get_scale_min_k4): bytes 0-3 the first four scales, 4-7 the first four
// mins, each with a top two bits of the last four's in its top two bits;
// bytes 8-11 the last four's low nibbles (scale low, min high).
struct KScales {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func f("k_scales");
        Var p, b;
        auto v = [&](const Expr &k) { return in[0](clamp(k, 0, 15), b, _); };
        auto u8 = [](int c) { return cast<uint8_t>(c); };
        Expr lo = v(p) | ((v(p + 4) >> 4) << 6), mid = v(p + 4) | ((v(p + 8) >> 4) << 6);
        Expr hi = (v(p - 4) & u8(15)) | ((v(p + 4) & u8(15)) << 4);
        f(p, b, _) = select(p < 4, lo, p < 8, mid, hi);
        return {f};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        Func f("k_scales_decoded");
        Var k, b;
        auto byte = [&](const Expr &i) { return e[0](clamp(i, 0, 11), b, _); };
        Expr s = k % 8, m = k / 8;
        Expr lo = byte(s + 4 * m) & cast<uint8_t>(63);
        Expr hi = ((byte(s + 4) >> cast<uint8_t>(4 * m)) & cast<uint8_t>(15)) | ((byte(s - 4 + 4 * m) >> 6) << 4);
        f(k, b, _) = select(s < 4, lo, hi);
        return {f};
    }
    ApproximationSignature signature(const ApproximationPorts &in) const {
        ApproximationPort i = in.empty() ? ApproximationPort("scales", UInt(8)) : in[0], o = i;
        i.range = ApproximationRange(0, 63);
        o.range = std::nullopt;
        return {{i}, {o}};
    }
    bool lossless() const {
        return true;
    }
};

// GGML's quantize_row_q4_K_ref. Per sub-block of 32: weights av_x + |x|
// (av_x the RMS), then make_qkx2_quants(32, 15, ..., -1, 0.1, 20): an affine
// fit (scale, min <= 0) to 4-bit codes, refined over 21 steps (each a
// least-squares fit to the codes at a trial iscale, kept if its weighted
// squared error is less; the first error's and each step's sums unfused). Per super-block: d = fp16(max scale / 63), dmin =
// fp16(max min / 63), each sub-block's scale and -min as 6-bit multiples of
// them (nearest_int, wrapping to uint8, at most 63), and the codes again,
// nearest_int((x + dmin * m) / (d * sc)) in [0, 15] (where d * sc is 0, the
// search's). Values = codes * (d * sc) - dmin * m.
struct Q4KQuant {
    static constexpr int n = 32, sub = QK_K / n;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func x = in[0], sx2("sum_x2"), w("weights"), scan("qkx2_scan"), init("qkx2_init"), sc("sub_scales"), lq("qkx2_codes");
        Func ms("max_scale"), mm("max_min"), lsm("sub_quants"), six("scales"), d("scale"), dmin("min"), q("codes");
        Var j, s, b, k;
        RDom r(0, n), r1(1, n - 1), rs(0, sub);
        Expr xr = x(s * n + r, b, _);
        sx2(s, b, _) = 0.0f;
        sx2(s, b, _) = fma(xr, xr, sx2(s, b, _));
        w(j, b, _) = sf(sqrt(sx2(j / n, b, _) / (float)n) + abs(x(j, b, _)));
        Expr wr = w(s * n + r, b, _);
        // min, max, sum_w, sum_x: GGML's scan from element 0
        Expr x0 = x(s * n, b, _), w0 = w(s * n, b, _), x1 = x(s * n + r1, b, _), w1 = w(s * n + r1, b, _);
        scan(s, b, _) = Tuple(x0, x0, w0, sf(w0 * x0));
        scan(s, b, _) = Tuple(sf(select(x1 < scan(s, b, _)[0], x1, scan(s, b, _)[0])), sf(select(x1 > scan(s, b, _)[1], x1, scan(s, b, _)[1])),
                              sf(scan(s, b, _)[2] + w1), fma(w1, x1, scan(s, b, _)[3]));
        Func ext("qkx2_range");
        ext(s, b, _) = Tuple(sf(select(scan(s, b, _)[0] > 0.0f, 0.0f, scan(s, b, _)[0])), scan(s, b, _)[1]);
        Expr lo = ext(s, b, _)[0], hi = ext(s, b, _)[1], sum_w = scan(s, b, _)[2], sum_x = scan(s, b, _)[3];
        // the codes of iscale and min, as floats
        auto codes = [&](const Expr &iscale, const Expr &m, const Expr &v) { return cast<float>(clamp(nearest_int(sf(iscale * (v - m))), 0, 15)); };
        auto sq = [](const Expr &e) { return e * e; };
        // the state: (best error, scale, min, the iscale and min of the best codes)
        Expr is0 = sf(15.0f / (hi - lo)), sc0 = sf(1.0f / is0);
        init(s, b, _) = 0.0f;
        init(s, b, _) = sf(init(s, b, _) + wr * sq(fma(sc0, codes(is0, lo, xr), lo) - xr));
        Func st("qkx2_state0");
        st(s, b, _) = Tuple(init(s, b, _), sc0, lo, is0, lo);
        for (int step = 0; step <= 20; step++) {
            std::string i = std::to_string(step);
            Func sums("qkx2_sums" + i), fit("qkx2_fit" + i), err("qkx2_err" + i), next("qkx2_state" + std::to_string(step + 1));
            float t = std::fma(0.1f, (float)step, -1.0f) + 15.0f;  // rmin + rdelta * is + nmax
            Expr m = st(s, b, _)[2], iscale = sf(t / (hi - m)), l = codes(iscale, m, xr), wl = sf(wr * l);
            sums(s, b, _) = Tuple(0.0f, 0.0f, 0.0f);
            sums(s, b, _) = Tuple(sf(sums(s, b, _)[0] + wl), sf(sums(s, b, _)[1] + wl * l), sf(sums(s, b, _)[2] + wl * xr));
            Expr s_l = sums(s, b, _)[0], s_l2 = sums(s, b, _)[1], s_xl = sums(s, b, _)[2];
            Expr D = sf(fma(sum_w, s_l2, -(s_l * s_l))), ts = sf(fma(sum_w, s_xl, -(sum_x * s_l)) / D), tm = sf(fma(s_l2, sum_x, -(s_l * s_xl)) / D);
            Expr pos = tm > 0.0f;
            fit(s, b, _) = Tuple(D, sf(select(pos, s_xl / s_l2, ts)), select(pos, 0.0f, tm));
            Expr fs = fit(s, b, _)[1], fm = fit(s, b, _)[2];
            err(s, b, _) = 0.0f;
            err(s, b, _) = sf(fma(wr, sq(fma(fs, l, fm) - xr), err(s, b, _)));
            Expr better = sf(fit(s, b, _)[0] > 0.0f && err(s, b, _) < st(s, b, _)[0]);
            next(s, b, _) = Tuple(select(better, err(s, b, _), st(s, b, _)[0]), select(better, fs, st(s, b, _)[1]), select(better, fm, m),
                                  select(better, iscale, st(s, b, _)[3]), select(better, m, st(s, b, _)[4]));
            st = next;
        }
        // max == min: codes 0, scale 0, min the (clamped) least value
        sc(s, b, _) = Tuple(select(sf(hi == lo), 0.0f, st(s, b, _)[1]), sf(-select(sf(hi == lo), lo, st(s, b, _)[2])));
        Expr js = j / n;
        lq(j, b, _) = select(sf(ext(js, b, _)[0] == ext(js, b, _)[1]), 0.0f, codes(st(js, b, _)[3], st(js, b, _)[4], x(j, b, _)));
        // the super-block's: the greatest scale and min (from 0)
        ms(b, _) = maximum(sf(select(sc(rs, b, _)[0] > 0.0f, sc(rs, b, _)[0], 0.0f)));
        mm(b, _) = maximum(sf(select(sc(rs, b, _)[1] > 0.0f, sc(rs, b, _)[1], 0.0f)));
        auto six_bits = [&](const Expr &m, const Expr &v) {
            Expr inv = sf(select(m > 0.0f, 63.0f / m, 0.0f));
            return min(cast<uint8_t>(nearest_int(sf(inv * v))), cast<uint8_t>(63));
        };
        lsm(s, b, _) = Tuple(six_bits(ms(b, _), sc(s, b, _)[0]), six_bits(mm(b, _), sc(s, b, _)[1]));
        six(k, b, _) = select(k < sub, lsm(k % sub, b, _)[0], lsm(k % sub, b, _)[1]);
        auto half = [](const Expr &v) { return cast<float>(fp16_round(v)); };
        d(b, _) = half(sf(ms(b, _) / 63.0f));
        dmin(b, _) = half(sf(mm(b, _) / 63.0f));
        Expr d1 = sf(d(b, _) * cast<float>(lsm(js, b, _)[0])), dm = sf(dmin(b, _) * cast<float>(lsm(js, b, _)[1]));
        Expr l = cast<float>(clamp(nearest_int(sf((x(j, b, _) + dm) / d1)), 0, 15));
        q(j, b, _) = cast<int8_t>(select(sf(d1 != 0.0f), l, lq(j, b, _)));
        return {q, six, d, dmin};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        Func v("values");
        Var j, b;
        Expr s = j / n;
        v(j, b, _) = cast<float>(e[0](j, b, _)) * (e[2](b, _) * cast<float>(e[1](s, b, _))) - e[3](b, _) * cast<float>(e[1](s + sub, b, _));
        return {v};
    }
    static ApproximationSignature signature() {
        return {{{"blocks", Float(32)}},
                {codes_port(0, 15), {"scales", UInt(8), std::nullopt, ApproximationRange(0, 63), ApproximationTags{tag::scale, tag::offset}}, param_port("scale", Float(32), tag::scale), param_port("min", Float(32), tag::offset)}};
    }
    // Codes nearest (x + dm) / d1 in [0, 15]: half a step, or the distance
    // past the codes' ends (d1 = 0: |x + dm|).
    Func error_bound(const std::vector<Func> &in, const std::vector<Func> &e) const {
        Func f("bound");
        Var j, b;
        auto f64 = [](const Expr &v) { return cast<double>(v); };
        Expr s = j / n, d1 = f64(e[2](b, _)) * f64(e[1](s, b, _)), dm = f64(e[3](b, _)) * f64(e[1](s + sub, b, _)), y = f64(in[0](j, b, _)) + dm;
        f(j, b, _) = max(Expr(0.5) * d1, -y, y - 15 * d1) * Expr(1.0001) + (abs(f64(in[0](j, b, _))) + dm + 15 * d1) * Expr(std::ldexp(1.0, -20));
        return f;
    }

private:
    static Expr fp16_round(const Expr &v) {
        return MiniFloat::binary16().encode(v);
    }
};

// q4_K's codes in [0, 15] <-> block_q4_K's qs: per 64 codes, 32 bytes, code
// i in the low nibble of byte i, code i + 32 in its high nibble.
inline Approximation q4_K_nibbles() {
    return Compose{BlockReshape{64}, PlanarFieldPack{4, 32}, Unblock{32}};
}

// block_q4_K {f16 d, dmin; u8 scales[12]; u8 qs[128]}.
inline Approximation q4_K(const std::string &lay, bool indexed = false) {
    return Compose{BlockReshape{QK_K, indexed}, Q4KQuant{}, Parallel{{"codes", unsigned_codes(4)}},
                   Parallel{{"codes", q4_K_nibbles()}, {"scales", KScales{}}, {"scale", fp16()}, {"min", fp16()}},
                   layout(lay, {{"d", Float(16)}, {"dmin", Float(16)}, {"scales", UInt(8), 12}, {"qs", UInt(8), QK_K / 2}}, {"qs", "scales", "d", "dmin"})};
}

// GGML's quantize_row_q6_K_ref. Per sub-block of 16: make_qx_quants(16, 32,
// ..., rmse_type 1): codes nearest_int(iscale * x) in [-32, 31], for iscale
// -32 / max (max the first value of the greatest |x|), then for each of 18
// trial iscales -(32 + 0.1 is) / max, is = -9..9 but 0, the first whose
// least-squares scale (weights x^2) beats the best so far; scale 0 (codes
// -32) for all-but-zero sub-blocks (max |x| < 1e-15). Per super-block: the
// first scale of the greatest magnitude, max, iscale = -128 / max, d =
// fp16(1 / iscale), each sub-block's scale min(127, nearest_int(iscale *
// scale)) (an int8), and the codes again, nearest_int(x / (d * sc)) in
// [-32, 31] (where d * sc is 0, the search's); all zero for blocks whose
// scales are below 1e-15. Values = codes * (d * sc).
struct Q6KQuant {
    static constexpr int n = 16, sub = QK_K / n, trials = 19;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func x = in[0], am("sub_amax"), cand("qx_trials"), st("qx_search"), sc("sub_scales"), lq("qx_codes");
        Func mas("max_abs_scale"), first("first_max_scale"), d("scale"), q("codes"), six("scales");
        Var j, s, c, b, k;
        RDom r(0, n), rk(1, trials - 1), rs(0, sub);
        Expr xr = x(s * n + r, b, _);
        // GGML's strict scan for the greatest |x|: (amax, max)
        am(s, b, _) = Tuple(0.0f, 0.0f);
        Expr gt = sf(abs(xr) > am(s, b, _)[0]);
        am(s, b, _) = Tuple(select(gt, abs(xr), am(s, b, _)[0]), select(gt, xr, am(s, b, _)[1]));
        Expr mx = am(s, b, _)[1];
        // -nmax, then -(nmax + 0.1 is) for is = -9..9 but 0
        std::vector<Expr> nums{-32.0f};
        for (int is = -9; is <= 9; is++) {
            if (is) nums.push_back(-std::fma(0.1f, (float)is, 32.0f));
        }
        auto iscale = [&](const Expr &t) { return sf(mux(t, nums) / mx); };
        auto codes = [&](const Expr &is, const Expr &v) { return clamp(nearest_int(sf(is * v)), -32, 31); };
        Expr cx = x(s * n + r, b, _), l = cast<float>(codes(iscale(c), cx)), wx = sf(cx * cx * cx);
        cand(c, s, b, _) = Tuple(0.0f, 0.0f);
        // (the first pass's sums are not fused: GGML's build vectorizes its products)
        Expr wl = sf(cx * cx * l), lx = cand(c, s, b, _)[0], l2 = cand(c, s, b, _)[1];
        cand(c, s, b, _) = Tuple(select(c == 0, sf(lx + wx * l), fma(wx, l, lx)), select(c == 0, sf(l2 + wl * l), fma(wl, l, l2)));
        // (scale, best, iscale)
        Expr lx0 = cand(0, s, b, _)[0], l20 = cand(0, s, b, _)[1], sc0 = sf(select(l20 != 0.0f, lx0 / l20, 0.0f));
        st(s, b, _) = Tuple(sc0, sf(sc0 * lx0), iscale(0));
        lx = cand(rk, s, b, _)[0], l2 = cand(rk, s, b, _)[1];
        Expr better = sf(l2 > 0.0f && lx * lx > st(s, b, _)[1] * l2), ns = sf(lx / l2);
        st(s, b, _) = Tuple(select(better, ns, st(s, b, _)[0]), select(better, sf(ns * lx), st(s, b, _)[1]), select(better, iscale(rk), st(s, b, _)[2]));
        sc(s, b, _) = select(sf(am(s, b, _)[0] < 1e-15f), 0.0f, st(s, b, _)[0]);
        Expr js = j / n;
        lq(j, b, _) = select(sf(am(js, b, _)[0] < 1e-15f), -32, codes(st(js, b, _)[2], x(j, b, _)));
        // the first scale of the greatest magnitude (order-free; NaN scales never are)
        mas(b, _) = maximum(sf(select(abs(sc(rs, b, _)) > 0.0f, abs(sc(rs, b, _)), 0.0f)));
        first(b, _) = minimum(select(abs(sc(rs, b, _)) == mas(b, _), rs, sub - 1));
        Expr zero = sf(mas(b, _) < 1e-15f), is = sf(-128.0f / sc(clamp(first(b, _), 0, sub - 1), b, _));
        d(b, _) = select(zero, 0.0f, cast<float>(MiniFloat::binary16().encode(sf(1.0f / is))));
        six(k, b, _) = select(zero, cast<int8_t>(0), cast<int8_t>(min(nearest_int(sf(is * sc(k, b, _))), 127)));
        Expr d1 = sf(d(b, _) * cast<float>(six(js, b, _)));
        q(j, b, _) = cast<int8_t>(select(zero, -32, sf(d1 != 0.0f), clamp(nearest_int(sf(x(j, b, _) / d1)), -32, 31), lq(j, b, _)));
        return {q, six, d};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        Func v("values");
        Var j, b;
        v(j, b, _) = cast<float>(e[0](j, b, _)) * (e[2](b, _) * cast<float>(e[1](j / n, b, _)));
        return {v};
    }
    static ApproximationSignature signature() {
        return {{{"blocks", Float(32)}}, {codes_port(-32, 31), param_port("scales", Int(8), tag::scale, ApproximationRange(-128, 127)), param_port("scale", Float(32), tag::scale)}};
    }
    // Codes nearest x / d1 in [-32, 31]: half a step, or the distance past
    // the codes' ends (d1 = 0: |x|).
    Func error_bound(const std::vector<Func> &in, const std::vector<Func> &e) const {
        Func f("bound");
        Var j, b;
        Expr d1 = cast<double>(e[2](b, _)) * cast<double>(e[1](j / n, b, _)), x = cast<double>(in[0](j, b, _));
        Expr lo = min(-32 * d1, 31 * d1), hi = max(-32 * d1, 31 * d1);
        f(j, b, _) = max(Expr(0.5) * abs(d1), x - hi, lo - x) * Expr(1.0001) + (abs(x) + 32 * abs(d1)) * Expr(std::ldexp(1.0, -20));
        return f;
    }
};

// block_q6_K {u8 ql[128]; u8 qh[64]; i8 scales[16]; f16 d}: codes + 32,
// their low nibbles in ql (per 128 codes, 64 bytes: code i low, i + 64
// high), their top two bits in qh (per 128 codes, 32 bytes: code i + 32 p in
// bits 2p).
inline Approximation q6_K(const std::string &lay, bool indexed = false) {
    return Compose{BlockReshape{QK_K, indexed}, Q6KQuant{}, Parallel{{"codes", SumSplit{4}}},
                   Parallel{{"low", Compose{BlockReshape{128}, PlanarFieldPack{4, 64}, Unblock{64}}},
                            {"high", Compose{multiples(4, -32, 4), BlockReshape{128}, PlanarFieldPack{2, 32}, Unblock{32}}},
                            {"scale", fp16()}},
                   layout(lay, {{"ql", UInt(8), QK_K / 2}, {"qh", UInt(8), QK_K / 4}, {"scales", Int(8), QK_K / 16}, {"d", Float(16)}}, {"ql", "qh", "scales", "d"})};
}

}  // namespace ggml
