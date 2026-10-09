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
// bytes; see Q4_0Quant). Then the fields' values to their storage.
inline Approximation q4_0(const Approximation &codes, const std::string &lay, int shift = 0) {
    Approximation codec = shift ? Parallel{{"codes", codes}, {"scale", scale_by(1 << shift)}} : Parallel{{"codes", codes}};
    return Compose{BlockReshape{QK}, Q4_0Quant{shift}, codec,
                   Parallel{{"codes", PlanarFieldPack{4, QK / 2}}, {"scale", fp16()}},
                   layout(lay, {{"d", Float(16)}, {"qs", UInt(8), QK / 2}}, {"qs", "d"})};
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

inline Approximation q8_0(const std::string &lay) {
    return Compose{BlockReshape{QK}, Q8_0Quant{}, Parallel{{"scale", fp16()}},
                   layout(lay, {{"d", Float(16)}, {"qs", Int(8), QK}}, {"qs", "d"})};
}

}  // namespace ggml
