#pragma once

// Units shared by GGML's block formats: blocks of QK values, an fp32 scale per
// block stored as fp16, codes stored as integers, and GGML's packed (AoS)
// block structs as the layout stage. Rank-polymorphic: extra dimensions (rows)
// pass through as implicit vars.
#include "Halide.h"

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
inline Approximation offset(int bias) {
    return Pointwise{"offset", [=](const Expr &x) { return cast<uint8_t>(x + bias); },
                     [=](const Expr &x) { return cast<int8_t>(cast<int>(x) - bias); }}
        .with_types(Int(8), UInt(8))
        .with_ranges(ApproximationRange(-bias, bias - 1), ApproximationRange(0, 2 * bias - 1))
        .with_lossless();
}

inline Approximation fp16() {
    return Pointwise{"fp16", [](const Expr &x) { return cast<float16_t>(x); },
                     [](const Expr &x) { return cast<float>(x); }}
        .with_types(Float(32), Float(16));
}

// GGML's on-disk block: `fields` in declaration order, packed; `logical` maps
// the incoming ports to them in order.
inline Approximation aos(std::vector<StructField> fields, std::vector<std::string> logical) {
    return StructLayout{Type::Struct(std::move(fields)), std::move(logical)};
}

}  // namespace ggml
