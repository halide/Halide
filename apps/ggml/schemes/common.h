#pragma once

// Units shared by GGML's block formats: blocks of QK values, an fp32 scale per
// block stored as fp16, codes stored as integers; the layout stage comes from
// layouts.h. Rank-polymorphic: extra dimensions (rows) pass through as
// implicit vars.
#include "layouts.h"

namespace ggml {

using namespace Halide;

constexpr int QK = 32;

inline Expr inverse(const Expr &d) {
    return select(d != 0.0f, 1.0f / d, 0.0f);
}

// C's roundf: half away from zero. Truncating v plus the largest float below
// 1/2 (with v's sign) equals roundf(v) for every float (checked exhaustively).
inline Expr round_away(const Expr &v) {
    const float h = 0.49999997f;
    return trunc(v + select(v < 0, -h, h));
}

// values = codes * scale, for codes(j, b, ...) and scale(b, ...).
inline Func dequant(const std::vector<Func> &e) {
    Func v("values");
    Var j, b;
    v(j, b, _) = cast<float>(e[0](j, b, _)) * e[1](b, _);
    return v;
}

// Error of at most `steps` quantization steps (plus float slack).
inline Func step_bound(const std::vector<Func> &e, double steps) {
    Func f("bound");
    Var j, b;
    f(j, b, _) = abs(cast<double>(e[1](b, _))) * Expr(steps * 1.0001);
    return f;
}

inline ApproximationSignature codes_and_scale(int lo, int hi) {
    return {{{"blocks", Float(32)}}, {{"codes", Int(8), std::nullopt, ApproximationRange(lo, hi)}, {"scale", Float(32)}}};
}

// Signed codes in [-bias, bias - 1] <-> unsigned fields in [0, 2 * bias - 1].
// With `shift`, codes kept at 2^shift times their value (multiples of 2^shift).
inline Approximation offset(int bias, int shift = 0) {
    int k = 1 << shift;
    return Pointwise{"offset", [=](const Expr &x) { return cast<uint8_t>((shift ? x >> shift : x) + bias); },
                     [=](const Expr &x) { return shift ? cast<int8_t>((x << shift) - cast<uint8_t>(bias * k)) : cast<int8_t>(cast<int>(x) - bias); }}
        .with_types(Int(8), UInt(8))
        .with_ranges(ApproximationRange(-bias * k, (bias - 1) * k), ApproximationRange(0, 2 * bias - 1))
        .with_lossless();
}

// Signed codes in [-2^(bits-1), 2^(bits-1) - 1] <-> their two's-complement
// `bits`-bit fields (GGML's repacked q4_0 nibbles: offset(8) XOR 8). With
// `shift`, codes kept at 2^shift times their value (multiples of 2^shift): at
// shift 8 - bits, a field's decode is its shift to the top of an int8.
inline Approximation twos(int bits, int shift = 0) {
    int h = 1 << (bits - 1), s = 8 - bits, k = 1 << shift;
    return Pointwise{"twos", [=](const Expr &x) { return cast<uint8_t>(shift ? x >> shift : x) & cast<uint8_t>(2 * h - 1); },
                     [=](const Expr &x) { Expr t = cast<int8_t>(x << s); return s > shift ? t >> (s - shift) : t; }}
        .with_types(Int(8), UInt(8))
        .with_ranges(ApproximationRange(-h * k, (h - 1) * k), ApproximationRange(0, 2 * h - 1))
        .with_lossless();
}

// x * k for a power of two k: exact for normal floats (and fp16's are).
inline Approximation scale_by(float k) {
    return Pointwise{"scale_by", [=](const Expr &x) { return x * k; }, [=](const Expr &x) { return x * (1.0f / k); }}
        .with_types(Float(32), Float(32))
        .with_lossless();
}

inline Approximation fp16() {
    return Pointwise{"fp16", [](const Expr &x) { return cast<float16_t>(x); },
                     [](const Expr &x) { return cast<float>(x); }}
        .with_types(Float(32), Float(16));
}

}  // namespace ggml
