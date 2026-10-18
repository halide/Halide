#pragma once

// GGML's legacy block formats (ggml-quants.c *_ref), as Approximations. Encode
// order: reshape to 32-element blocks, quantize, store the fields, lay out
// (layouts.h).
#include "common.h"

namespace ggml {

// block_q4_0: d = the signed extreme / -8 (-0 for all-zero); nibble = min(15, (int8)(x / d + 8.5)).
// With `code_shift`, the power of two 2^code_shift moves from the scale into
// the codes: (c * 2^code_shift, d / 2^code_shift), decoded alike (codes *
// scale). Its decode is bit-exact with the unshifted one: both products are
// the same real number as long as the scale stays a normal float, as fp16
// scales do; a kernel's other products (e.g. an integer dot times the
// scales) are exact alike, barring subnormal float intermediates.
struct Q4_0Quant {
    int code_shift = 0;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func x = in[0], d("scale"), q("codes");
        Var j, b;
        RDom r(0, QK);
        Tuple m = argmax(abs(x(r, b, _)));  // first strict max, as GGML's scan
        d(b, _) = select(x(clamp(m[0], 0, QK - 1), b, _) < 0, m[1], -m[1]) / 8.0f;
        q(j, b, _) = min(cast<int8_t>(x(j, b, _) * inverse(d(b, _)) + 8.5f), cast<int8_t>(15)) - 8;
        if (!code_shift) return {q, d};
        Func qs("codes"), ds("scale");
        qs(j, b, _) = q(j, b, _) * cast<int8_t>(1 << code_shift);
        ds(b, _) = d(b, _) * (1.0f / (1 << code_shift));
        return {qs, ds};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        return {dequant(e)};
    }
    ApproximationSignature signature() const {
        return codes_and_scale(-8 * (1 << code_shift), 7 * (1 << code_shift));
    }
    Func error_bound(const std::vector<Func> &, const std::vector<Func> &e) const {
        return step_bound(e, 1 << code_shift);  // the top code is clamped: +8 steps becomes 7
    }
};

// `codes`: the nibble encoding (GGML's block_q4_0: offset(8); its repacked
// layouts: twos(4)) of the codes, kept at 2^shift times their value (the same
// bytes; see Q4_0Quant). Then the fields' values to their storage. `indexed`:
// the values' side is (index in block, block, ...) rather than flat.
inline Approximation q4_0(const Approximation &codes, const std::string &lay, int shift = 0, bool indexed = false) {
    Approximation codec = shift ? Parallel{{"codes", codes}, {"scale", scale_by(1 << shift)}} : Parallel{{"codes", codes}};
    return Compose{BlockReshape{QK, indexed}, Q4_0Quant{shift}, codec,
                   Parallel{{"codes", PlanarFieldPack{4, QK / 2}}, {"scale", fp16()}},
                   layout(lay, {{"d", Float(16)}, {"qs", UInt(8), QK / 2}}, {"qs", "d"})};
}

// block_q4_1: affine, values = codes * scale + min; d = (max - min) / 15,
// min = the block's (both first strict extremes, as GGML's scan); nibble =
// min(15, (int8)((x - min) / d + 0.5)).
struct Q4_1Quant {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func x = in[0], d("scale"), m("min"), q("codes");
        Var j, b;
        RDom r(0, QK);
        m(b, _) = argmin(x(r, b, _))[1];
        d(b, _) = (argmax(x(r, b, _))[1] - m(b, _)) / 15.0f;
        q(j, b, _) = min(cast<int8_t>((x(j, b, _) - m(b, _)) * inverse(d(b, _)) + 0.5f), cast<int8_t>(15));
        return {q, d, m};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        Func v("values");
        Var j, b;
        v(j, b, _) = cast<float>(e[0](j, b, _)) * e[1](b, _) + e[2](b, _);
        return {v};
    }
    static ApproximationSignature signature() {
        return {{{"blocks", Float(32)}}, {{"codes", Int(8), std::nullopt, ApproximationRange(0, 15)}, {"scale", Float(32)}, {"min", Float(32)}}};
    }
    Func error_bound(const std::vector<Func> &, const std::vector<Func> &e) const {
        return step_bound(e, 0.5);
    }
};

inline Approximation q4_1(const std::string &lay, bool indexed = false) {
    return Compose{BlockReshape{QK, indexed}, Q4_1Quant{}, Parallel{{"codes", unsigned_codes(4)}},
                   Parallel{{"codes", PlanarFieldPack{4, QK / 2}}, {"scale", fp16()}, {"min", fp16()}},
                   layout(lay, {{"d", Float(16)}, {"m", Float(16)}, {"qs", UInt(8), QK / 2}}, {"qs", "d", "m"})};
}

// block_q8_0: d = max |x| / 127; code = roundf(x / d).
struct Q8_0Quant {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func x = in[0], d("scale"), q("codes");
        Var j, b;
        RDom r(0, QK);
        d(b, _) = maximum(abs(x(r, b, _))) / 127.0f;
        q(j, b, _) = cast<int8_t>(round_away(x(j, b, _) * inverse(d(b, _))));
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

inline Approximation q8_0(const std::string &lay, bool indexed = false) {
    return Compose{BlockReshape{QK, indexed}, Q8_0Quant{}, Parallel{{"scale", fp16()}},
                   layout(lay, {{"d", Float(16)}, {"qs", Int(8), QK}}, {"qs", "d"})};
}

// The block's sum of its values, scale * sum(codes), from codes(j, b, ...)
// and scale(b, ...): block_q8_1's s, as a derived port.
inline Func block_sum(const std::vector<Func> &from) {
    Func s("block_sum");
    Var b;
    RDom r(0, QK);
    s(b, _) = cast<float>(from[1](b, _)) * cast<float>(sum(cast<int>(from[0](r, b, _))));
    return s;
}

// block_q8_1 (GGML's activations for q4_1, q5_1): q8_0's codes and scale, plus
// s, the block's sum, derived from the codes and the scale once rounded to
// fp16 (so it is the sum of the decoded block), then rounded to fp16 itself:
// within half an fp16 ulp of the sum of the decoded values, which kernels may
// read instead (Func::approximate_by(DerivedPort, ...)). Decode ignores it.
// GGML derives s from the unrounded scale (fp16(d * sum(qs))), an fp16 ulp
// off this in about a quarter of the blocks, and then not the sum of the
// decoded block (nor idempotent): the scheme is GGML's but for that.
// q8_1_stages() are the stages before the layout; with ggml_sum, s is derived
// GGML's way (for the property test, which shows what that breaks).
inline std::vector<Approximation> q8_1_stages(bool indexed = false, bool ggml_sum = false) {
    Derive sum{"sum", {"codes", "scale"}, block_sum};
    if (ggml_sum) {
        return {BlockReshape{QK, indexed}, Q8_0Quant{}, sum, Parallel{{"scale", fp16()}, {"sum", fp16()}}};
    }
    return {BlockReshape{QK, indexed}, Q8_0Quant{}, Parallel{{"scale", fp16()}}, sum, Parallel{{"sum", fp16()}}};
}

inline Approximation q8_1(const std::string &lay, bool indexed = false) {
    std::vector<Approximation> stages = q8_1_stages(indexed);
    stages.push_back(layout(lay, {{"d", Float(16)}, {"s", Float(16)}, {"qs", Int(8), QK}}, {"qs", "d", "s"}));
    return Compose{stages};
}

}  // namespace ggml
