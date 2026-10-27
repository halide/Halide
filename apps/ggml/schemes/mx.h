#pragma once

// GGML's OCP MX block format (block_mxfp4, ggml-quants.c *_ref), as an
// Approximation: 32 e2m1 values (MiniFloat) sharing an e8m0 scale (a
// MiniFloat), with GGML's choice of that scale and of the codes.
#include "common.h"

namespace ggml {

// The codes: e2m1 (OCP MX's FP4: no inf or NaN), doubled to integers, as
// GGML's kvalues_mxfp4 (its index the field).
inline MiniFloat e2m1() {
    return {2, 1, 1, true, MiniFloat::Specials::None, MiniFloat::Overflow::Saturate};
}

// GGML's block_mxfp4 quantization: e = (uint8_t)(floorf(log2f(amax)) - 2 +
// 127), wrapping (0 for amax = 0), its scale d = 2^(e - 128)
// (GGML_E8M0_TO_FP32_HALF: half of e8m0's 2^(e - 127), 2^127 for e = 255),
// and each code the doubled e2m1 value (kvalues_mxfp4) whose multiple of d is
// nearest x (best_index_mxfp4: the first of kvalues_mxfp4 at the least
// |kv * d - x|, in float: ties to the lesser magnitude, 0 positive). log2f
// as correctly rounded: log2 in double, rounded to float (the same floor as
// Apple's log2f on every positive float). Blocks of tiny values
// (floorf(log2f(amax)) < -125) make e wrap to 255 and down: d is then far
// above them, and their codes 0.
struct MXFP4Quant {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func x = in[0], amax("amax"), e("exponent"), d("scale"), q("codes");
        Var j, b;
        RDom r(0, QK), k(1, 15);
        amax(b, _) = argmax(abs(x(r, b, _)))[1];  // GGML's strict scan: NaNs skipped
        Expr l = strict_float(floor(cast<float>(log(cast<double>(amax(b, _))) * Expr(1 / std::log(2.0)))));
        e(b, _) = select(amax(b, _) > 0.0f, cast<uint8_t>(cast<int>(l) + 125), cast<uint8_t>(0));
        Expr u = cast<uint32_t>(e(b, _));
        d(b, _) = reinterpret<float>(select(u < 2, cast<uint32_t>(0x00200000) << u, (u - 1) << 23));
        q(j, b, _) = best_index(x, d(b, _), j, b, k);
        return {q, d};
    }
    // best_index_mxfp4(x(j, b, ...), d) (d an Expr in j, b; k: RDom(1, 15),
    // the codes searched after the first): the code, by
    // GGML's scan as its arm64 build computes it: |kv * d - x| fused (clang
    // contracts it), so exact but for its rounding once kv * d passes f32's
    // range
    static Expr best_index(const Func &x, const Expr &d, const Var &j, const Var &b, const RDom &k) {
        Func search("code_search");
        std::vector<Expr> kvs;
        for (int v : kvalues()) {
            kvs.push_back(cast<int8_t>(v));
        }
        auto err = [&](const Expr &kv) { return strict_float(abs(fma(cast<float>(kv), d, -x(j, b, _)))); };
        search(j, b, _) = Tuple(kvs[0], err(kvs[0]));
        Expr kv = mux(k, kvs), better = strict_float(err(kv) < search(j, b, _)[1]);
        search(j, b, _) = Tuple(select(better, kv, search(j, b, _)[0]), select(better, err(kv), search(j, b, _)[1]));
        return search(j, b, _)[0];
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        return {dequant(e)};
    }
    static ApproximationSignature signature() {
        return codes_and_scale(-12, 12);
    }
    // |x| < 16 d: at most 4 steps off (past 14 d, to 12 d)
    Func error_bound(const std::vector<Func> &, const std::vector<Func> &e) const {
        return step_bound(e, 4);
    }

    // kvalues_mxfp4: each e2m1 field's doubled value, in field order
    static std::vector<int> kvalues() {
        Func f("fields");
        Var i;
        f(i) = cast<uint8_t>(i);
        Buffer<int8_t> kv = minifloat_integers(e2m1()).decode({f}).decoded[0].realize({16});
        return std::vector<int>(kv.begin(), kv.end());
    }
};

// The codes <-> their e2m1 fields, through kvalues_mxfp4 as a codebook (its
// decode a table lookup, as GGML's NEON vqtbl1q_s8; `lut`: a staged table,
// see codebook()).
inline Approximation e2m1_codes(bool lut = false) {
    return codebook(e2m1().name() + "_codes", MXFP4Quant::kvalues(), lut);
}

// GGML's e8m0 scales: d = 2^(e - 128), half of e8m0's value (as
// GGML_E8M0_TO_FP32_HALF): an e8m0 MiniFloat (OCP MX's: byte 0xFF NaN, so
// 2^128 encodes to it) of 2d, and decoded GGML's way, with 0xFF as 2^127.
// Lossless on the powers of two 2^-128 .. 2^127. The decode builds d's bits
// as (e - 1) << 23 (d's exponent field, e >= 2), else, as the larger,
// (e + 1) << 21 (its subnormals 2^-128 and 2^-127, e < 2). Interleaved
// records decode a vector of scales, cheaper as d's top 16 bits in int16
// (eight lanes to a register, widened by the shift); a flat layout's one
// scale per block is cheaper in int32.
inline Approximation e8m0_half(bool narrow = false) {
    MiniFloat f{8, 0, 127, false, MiniFloat::Specials::NaNOnly};
    return Pointwise{"e8m0_half", [=](const Expr &x) { return f.encode(x * 2.0f); },
                     [=](const Expr &x) {
                         if (!narrow) {
                             Expr e = cast<int32_t>(x);
                             return reinterpret<float>(max((e - 1) << 23, (e + 1) << 21));
                         }
                         Expr e = cast<int16_t>(x), top = max((e - 1) << 7, (e + 1) << 5);
                         return reinterpret<float>(cast<uint32_t>(cast<uint16_t>(top)) << 16);
                     }}
        .with_types(Float(32), UInt(8))
        .with_lossless();
}

// block_mxfp4: the codes' fields packed as q4_0's (code j in the low nibble
// of byte j, code j + 16 in its high nibble), the scale's byte e before them.
inline Approximation mxfp4(const std::string &lay, bool indexed = false, bool lut = false) {
    return Compose{BlockReshape{QK, indexed}, MXFP4Quant{}, Parallel{{"codes", e2m1_codes(lut)}, {"scale", e8m0_half(isdigit(lay[0]))}},
                   Parallel{{"codes", PlanarFieldPack{4, QK / 2}}},
                   layout(lay, {{"e", UInt(8)}, {"qs", UInt(8), QK / 2}}, {"qs", "e"})};
}

// NVFP4's scales: ue4m3, unsigned e4m3 (OCP's e4m3fn, unsigned) rounded to
// nearest, ties away, as GGML's ggml_fp32_to_ue4m3 rounds its normals, with
// its quirks: x > 0 only (NaN 0); exponent 15 (a rounded x of 256 or more)
// saturated to 448 (0x7E); below the normals (x < 2^-6), the subnormal
// mantissa (int)(x * 512 + 0.5f) (the sum in f32), at most 7 (not rounding up
// to 2^-6). Decoded as ggml_ue4m3_to_fp32: the low 7 bits as an e4m3 with no
// specials (0xFF is 240), 0x7F (e4m3fn's NaN) as 0, and halved (GGML's scale
// of kvalues_mxfp4, which double e2m1's values).
inline MiniFloat ue4m3() {
    return {4, 3, 7, false, MiniFloat::Specials::None, MiniFloat::Overflow::Saturate, MiniFloat::Rounding::NearestAway};
}
inline Expr ue4m3_ggml_encode(const Expr &x) {
    Expr c = ue4m3().encode(x), sub = min(cast<int>(strict_float(min(x, 1.0f) * 512.0f + 0.5f)), 7);
    c = select(c > cast<uint8_t>(0x77), cast<uint8_t>(0x7E), c);
    c = select(x < (float)std::ldexp(1.0, -6), cast<uint8_t>(sub), c);
    return select(strict_float(x > 0.0f), c, cast<uint8_t>(0));
}
inline Expr ue4m3_ggml_decode(const Expr &c) {
    return select(c == cast<uint8_t>(0x7F), 0.0f, ue4m3().decode(c) * 0.5f);
}

// GGML's ue4m3 scales d (halved, as ggml_ue4m3_to_fp32) <-> their bytes:
// lossless on the scales GGML's encode yields (all decoded bytes but 0x78 ..
// 0x7D and those above 0x7E).
inline Approximation ue4m3_half() {
    return Pointwise{"ue4m3_half", [](const Expr &x) { return ue4m3_ggml_encode(x * 2.0f); }, [](const Expr &x) { return ue4m3_ggml_decode(x); }}
        .with_types(Float(32), UInt(8))
        .with_lossless();
}

// GGML's block_nvfp4 quantization (quantize_row_nvfp4_ref): per sub-block of
// 16, amax by GGML's strict scan, the scale d = ue4m3(amax / 6) (decoded,
// halved), and each code best_index_mxfp4's at d. Values = codes * d.
struct NVFP4Quant {
    static constexpr int n = 16, block = 64;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func x = in[0], amax("sub_amax"), d("scales"), q("codes");
        Var j, s, b;
        RDom r(0, n), k(1, 15);
        // GGML's strict scan, order-free: NaNs skipped
        Expr a = abs(x(s * n + r, b, _));
        amax(s, b, _) = maximum(strict_float(select(a > 0.0f, a, 0.0f)));
        d(s, b, _) = ue4m3_ggml_decode(ue4m3_ggml_encode(strict_float(amax(s, b, _) / 6.0f)));
        q(j, b, _) = MXFP4Quant::best_index(x, d(j / n, b, _), j, b, k);
        return {q, d};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        Func v("values");
        Var j, b;
        v(j, b, _) = cast<float>(e[0](j, b, _)) * e[1](j / n, b, _);
        return {v};
    }
    static ApproximationSignature signature() {
        return {{{"blocks", Float(32)}}, {codes_port(-12, 12), param_port("scales", Float(32), tag::scale)}};
    }
    // The codes nearest x / d (at most 4 apart: half that), or past their ends
    // (12 d, as amax / 6 saturates at 448)
    Func error_bound(const std::vector<Func> &in, const std::vector<Func> &e) const {
        Func f("bound");
        Var j, b;
        Expr d = cast<double>(e[1](j / n, b, _)), x = cast<double>(in[0](j, b, _));
        f(j, b, _) = max(2 * d, abs(x) - 12 * d) * Expr(1.0001) + (abs(x) + 12 * d) * Expr(std::ldexp(1.0, -20));
        return f;
    }
};

// block_nvfp4 {u8 d[4]; u8 qs[32]}: per sub-block of 16, its scale's byte
// in d and 8 bytes of its codes' fields packed as q4_0's (code j in the low
// nibble of byte j, code j + 8 in its high nibble). `tables`: the scales
// decoded through a table of each byte's (as GGML's NEON vec_dot: clang
// tabulates its ggml_ue4m3_to_fp32).
inline Approximation nvfp4(const std::string &lay, bool indexed = false, bool tables = false) {
    const int n = NVFP4Quant::n, block = NVFP4Quant::block;
    Approximation scales = tables ? Approximation(Tabulate{ue4m3_half(), 1}) : ue4m3_half();
    return Compose{BlockReshape{block, indexed}, NVFP4Quant{}, Parallel{{"codes", e2m1_codes()}, {"scales", scales}},
                   Parallel{{"codes", Compose{BlockReshape{n}, PlanarFieldPack{4, n / 2}, Unblock{n / 2}}}},
                   layout(lay, {{"d", UInt(8), block / n}, {"qs", UInt(8), block / 2}}, {"qs", "d"})};
}

}  // namespace ggml
