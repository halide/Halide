#pragma once

// GGML's 1-bit format (ggml-quants.c quantize_row_q1_0_ref), as an
// Approximation: the signs of 128 values times their mean magnitude.
#include "common.h"
#include "kquants.h"

namespace ggml {

// The sign quantization: d = the sum of |x| in order (GGML's sequential
// float sum), divided by the block's size; code = 1 for x >= 0 (so for -0,
// not for NaN), else -1. Values = codes * d. The error of a value is
// ||x| - d|, bounded by |x| + d.
struct SignQuant {
    static constexpr int block = 128;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func x = in[0], s("sum_abs"), d("scale"), q("codes");
        Var j, b;
        RDom r(0, block);
        s(rows_of(x, {b})) = 0.0f;
        s(b, _) = sf(s(b, _) + abs(x(r, b, _)));
        d(b, _) = sf(s(b, _) / block);
        q(j, b, _) = cast<int8_t>(select(x(j, b, _) >= 0.0f, 1, -1));
        return {q, d};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        return {dequant(e)};
    }
    ApproximationSignature signature() const {
        return codes_and_scale(-1, 1);
    }
    Func error_bound(const std::vector<Func> &in, const std::vector<Func> &e) const {
        Func f("bound");
        Var j, b;
        f(j, b, _) = abs(cast<double>(in[0](j, b, _))) + abs(cast<double>(e[1](b, _))) * Expr(1.001);
        return f;
    }
};

// block_q1_0 {f16 d; u8 qs[16]}: bit j set for the code 1, least
// significant first.
inline Approximation q1_0(const std::string &lay, bool indexed = false) {
    return Compose{BlockReshape{SignQuant::block, indexed}, SignQuant{}, Parallel{{"codes", multiples(1, -1, 2)}, {"scale", fp16()}},
                   Parallel{{"codes", BitPack{1}}},
                   layout(lay, {{"d", Float(16)}, {"qs", UInt(8), SignQuant::block / 8}}, {"qs", "d"})};
}

}  // namespace ggml
