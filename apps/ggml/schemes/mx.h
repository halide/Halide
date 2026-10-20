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
        Func x = in[0], amax("amax"), e("exponent"), d("scale"), search("code_search"), q("codes");
        Var j, b;
        RDom r(0, QK), k(1, 15);
        amax(b, _) = argmax(abs(x(r, b, _)))[1];  // GGML's strict scan: NaNs skipped
        Expr l = strict_float(floor(cast<float>(log(cast<double>(amax(b, _))) * Expr(1 / std::log(2.0)))));
        e(b, _) = select(amax(b, _) > 0.0f, cast<uint8_t>(cast<int>(l) + 125), cast<uint8_t>(0));
        Expr u = cast<uint32_t>(e(b, _));
        d(b, _) = reinterpret<float>(select(u < 2, cast<uint32_t>(0x00200000) << u, (u - 1) << 23));
        // best_index_mxfp4's scan, as GGML's arm64 build computes it: |kv * d
        // - x| fused (clang contracts it), so exact but for its rounding once
        // kv * d passes f32's range
        std::vector<Expr> kvs;
        for (int v : kvalues()) {
            kvs.push_back(cast<int8_t>(v));
        }
        auto err = [&](const Expr &kv) { return strict_float(abs(fma(cast<float>(kv), d(b, _), -x(j, b, _)))); };
        search(j, b, _) = Tuple(kvs[0], err(kvs[0]));
        Expr kv = mux(k, kvs), better = strict_float(err(kv) < search(j, b, _)[1]);
        search(j, b, _) = Tuple(select(better, kv, search(j, b, _)[0]), select(better, err(kv), search(j, b, _)[1]));
        q(j, b, _) = search(j, b, _)[0];
        return {q, d};
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
// decode a table lookup, as GGML's NEON vqtbl1q_s8).
inline Approximation e2m1_codes() {
    return codebook(e2m1().name() + "_codes", MXFP4Quant::kvalues());
}

// GGML's e8m0 scales: d = 2^(e - 128), half of e8m0's value (as
// GGML_E8M0_TO_FP32_HALF): an e8m0 MiniFloat (OCP MX's: byte 0xFF NaN, so
// 2^128 encodes to it) of 2d, and decoded GGML's way, with 0xFF as 2^127.
// Lossless on the powers of two 2^-128 .. 2^127.
inline Approximation e8m0_half() {
    MiniFloat f{8, 0, 127, false, MiniFloat::Specials::NaNOnly};
    return Pointwise{"e8m0_half", [=](const Expr &x) { return f.encode(x * 2.0f); },
                     [=](const Expr &x) { return select(x == cast<uint8_t>(255), (float)std::ldexp(1.0, 127), f.decode(x) * 0.5f); }}
        .with_types(Float(32), UInt(8))
        .with_lossless();
}

// block_mxfp4: the codes' fields packed as q4_0's (code j in the low nibble
// of byte j, code j + 16 in its high nibble), the scale's byte e before them.
inline Approximation mxfp4(const std::string &lay, bool indexed = false) {
    return Compose{BlockReshape{QK, indexed}, MXFP4Quant{}, Parallel{{"codes", e2m1_codes()}, {"scale", e8m0_half()}},
                   Parallel{{"codes", PlanarFieldPack{4, QK / 2}}},
                   layout(lay, {{"e", UInt(8)}, {"qs", UInt(8), QK / 2}}, {"qs", "e"})};
}

}  // namespace ggml
