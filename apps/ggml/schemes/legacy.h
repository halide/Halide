#pragma once

// GGML's legacy block formats (ggml-quants.c *_ref), as Approximations. Encode
// order: reshape to 32-element blocks, quantize, store the fields, lay out.
#include "common.h"

namespace ggml {

// block_q4_0: d = the signed extreme / -8 (-0 for all-zero); nibble = min(15, (int8)(x / d + 8.5)).
struct Q4_0Quant {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func x = in[0], d("scale"), q("codes");
        Var j, b;
        RDom r(0, QK);
        Tuple m = argmax(abs(x(r, b, _)));  // first strict max, as GGML's scan
        d(b, _) = select(x(clamp(m[0], 0, QK - 1), b, _) < 0, m[1], -m[1]) / 8.0f;
        q(j, b, _) = min(cast<int8_t>(x(j, b, _) * inverse(d(b, _)) + 8.5f), cast<int8_t>(15)) - 8;
        return {q, d};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        return {dequant(e)};
    }
    static ApproximationSignature signature() {
        return codes_and_scale(-8, 7);
    }
    Func error_bound(const std::vector<Func> &, const std::vector<Func> &e) const {
        return step_bound(e, 1);  // the top code is clamped: +8 steps becomes 7
    }
};

inline Approximation q4_0() {
    return Compose{BlockReshape{QK}, Q4_0Quant{},
                   Parallel{{"codes", Compose{offset(8), PlanarFieldPack{4, QK / 2}}}, {"scale", fp16()}},
                   aos({{"d", Float(16)}, {"qs", UInt(8), QK / 2}}, {"qs", "d"})};
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

inline Approximation q8_0() {
    return Compose{BlockReshape{QK}, Q8_0Quant{}, Parallel{{"scale", fp16()}},
                   aos({{"d", Float(16)}, {"qs", Int(8), QK}}, {"qs", "d"})};
}

}  // namespace ggml
