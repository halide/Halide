#pragma once

// Shared per-element value helpers for GGML's 3 "activation" formats (the
// vec_dot_type every quantized weight type is paired against). Unlike the
// weight types, these 3 formats are reused identically by every vec_dot
// kernel in this directory (see the *_generators.cpp files' VecDot
// generators), so factoring them out here avoids 24 copies of the same 3
// small formulas -- this is data/logic these files would otherwise all
// duplicate verbatim, not a design abstraction over the weight types
// themselves (each weight type's own dequantize math stays inline in its
// own file, per the project's "no shared abstraction across types" rule).
//
// vec_dot itself is a tolerance-checked (not bit-exact) benchmark, so the
// dot product is computed here as a plain elementwise dequantize-then-
// multiply-and-sum -- mathematically equivalent to GGML's integer-
// factored accumulation (see each block's affine formula), just re-grouped.
//
// Block layouts (see q8_0_generators.cpp / q8_1_generators.cpp /
// q8_k_generators.cpp for the authoritative comments):
//   Q8_0: 34 bytes/block, 32 elements -- fp16 d, then 32 signed int8 qs.
//   Q8_1: 36 bytes/block, 32 elements -- fp16 d, fp16 s (unused here), then
//         32 signed int8 qs.
//   Q8_K: 292 bytes/block, 256 elements -- float32 d, then 256 signed int8
//         qs, then 16 int16 bsums (unused here).

#include "Halide.h"

namespace ggml_halide {

inline Halide::Expr q8_0_value(const Halide::GeneratorInput<Halide::Buffer<uint8_t, 2>> &blocks, Halide::Expr x) {
    using namespace Halide;
    constexpr int kQK = 32;
    Expr i = x / kQK;
    Expr j = x % kQK;
    Expr d_lo = cast<uint16_t>(blocks(0, i));
    Expr d_hi = cast<uint16_t>(blocks(1, i));
    Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
    Expr q = reinterpret<int8_t>(blocks(2 + j, i));
    return d * cast<float>(q);
}

inline Halide::Expr q8_1_value(const Halide::GeneratorInput<Halide::Buffer<uint8_t, 2>> &blocks, Halide::Expr x) {
    using namespace Halide;
    constexpr int kQK = 32;
    Expr i = x / kQK;
    Expr j = x % kQK;
    Expr d_lo = cast<uint16_t>(blocks(0, i));
    Expr d_hi = cast<uint16_t>(blocks(1, i));
    Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
    Expr q = reinterpret<int8_t>(blocks(4 + j, i));
    return d * cast<float>(q);
}

inline Halide::Expr q8_k_value(const Halide::GeneratorInput<Halide::Buffer<uint8_t, 2>> &blocks, Halide::Expr x) {
    using namespace Halide;
    constexpr int kQK_K = 256;
    Expr i = x / kQK_K;
    Expr j = x % kQK_K;
    Expr b0 = cast<uint32_t>(blocks(0, i));
    Expr b1 = cast<uint32_t>(blocks(1, i));
    Expr b2 = cast<uint32_t>(blocks(2, i));
    Expr b3 = cast<uint32_t>(blocks(3, i));
    Expr d = reinterpret<float>(b0 | (b1 << 8) | (b2 << 16) | (b3 << 24));
    Expr q = reinterpret<int8_t>(blocks(4 + j, i));
    return d * cast<float>(q);
}

}  // namespace ggml_halide
