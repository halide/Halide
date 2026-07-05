#include "ggml_quants.h"

#include <cstdio>

#include "HalideBuffer.h"
#include "bf16_dequantize.h"
#include "bf16_quantize.h"
#include "f16_dequantize.h"
#include "f16_quantize.h"
#include "iq1_m_dequantize.h"
#include "iq1_s_dequantize.h"
#include "iq2_s_dequantize.h"
#include "iq2_s_quantize.h"
#include "iq2_xs_dequantize.h"
#include "iq2_xxs_dequantize.h"
#include "iq3_s_dequantize.h"
#include "iq3_s_quantize.h"
#include "iq3_xxs_dequantize.h"
#include "iq3_xxs_quantize.h"
#include "iq4_nl_dequantize.h"
#include "iq4_nl_quantize.h"
#include "iq4_xs_dequantize.h"
#include "iq4_xs_quantize.h"
#include "mxfp4_dequantize.h"
#include "mxfp4_quantize.h"
#include "nvfp4_dequantize.h"
#include "nvfp4_quantize.h"
#include "q1_0_dequantize.h"
#include "q1_0_quantize.h"
#include "q2_k_dequantize.h"
#include "q2_k_quantize.h"
#include "q3_k_dequantize.h"
#include "q3_k_quantize.h"
#include "q4_0_dequantize.h"
#include "q4_0_quantize.h"
#include "q4_1_dequantize.h"
#include "q4_1_quantize.h"
#include "q4_k_dequantize.h"
#include "q4_k_quantize.h"
#include "q5_0_dequantize.h"
#include "q5_0_quantize.h"
#include "q5_1_dequantize.h"
#include "q5_1_quantize.h"
#include "q5_k_dequantize.h"
#include "q5_k_quantize.h"
#include "q6_k_dequantize.h"
#include "q6_k_quantize.h"
#include "q8_0_dequantize.h"
#include "q8_0_quantize.h"
#include "q8_1_quantize.h"
#include "q8_k_quantize.h"
#include "tq1_0_dequantize.h"
#include "tq1_0_quantize.h"
#include "tq2_0_dequantize.h"
#include "tq2_0_quantize.h"

using Halide::Runtime::Buffer;

namespace {

void check(int result, const char *what) {
    if (result != 0) {
        std::fprintf(stderr, "ggml_quants_halide: %s failed (%d)\n", what, result);
    }
}

}  // namespace

extern "C" {

//
// Q4_0 -- block size 32, 18 bytes/block (2 delta + 16 packed nibbles).
//

void ggml_quants_halide_quantize_q4_0(const float *x, void *y, int64_t k) {
    constexpr int kQK = 32, kBlockBytes = 2 + kQK / 2;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(q4_0_quantize(xb, blocks), "q4_0_quantize");
}

void ggml_quants_halide_dequantize_q4_0(const void *x, float *y, int64_t k) {
    constexpr int kQK = 32, kBlockBytes = 2 + kQK / 2;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(q4_0_dequantize(blocks, yb), "q4_0_dequantize");
}

//
// Q4_1 -- block size 32, 20 bytes/block (2 delta + 2 min + 16 packed nibbles).
//

void ggml_quants_halide_quantize_q4_1(const float *x, void *y, int64_t k) {
    constexpr int kQK = 32, kBlockBytes = 4 + kQK / 2;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(q4_1_quantize(xb, blocks), "q4_1_quantize");
}

void ggml_quants_halide_dequantize_q4_1(const void *x, float *y, int64_t k) {
    constexpr int kQK = 32, kBlockBytes = 4 + kQK / 2;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(q4_1_dequantize(blocks, yb), "q4_1_dequantize");
}

//
// Q5_0 -- block size 32, 22 bytes/block (2 delta + 4 qh + 16 packed nibbles).
//

void ggml_quants_halide_quantize_q5_0(const float *x, void *y, int64_t k) {
    constexpr int kQK = 32, kBlockBytes = 2 + 4 + kQK / 2;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(q5_0_quantize(xb, blocks), "q5_0_quantize");
}

void ggml_quants_halide_dequantize_q5_0(const void *x, float *y, int64_t k) {
    constexpr int kQK = 32, kBlockBytes = 2 + 4 + kQK / 2;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(q5_0_dequantize(blocks, yb), "q5_0_dequantize");
}

//
// Q5_1 -- block size 32, 24 bytes/block (2 delta + 2 min + 4 qh + 16 packed nibbles).
//

void ggml_quants_halide_quantize_q5_1(const float *x, void *y, int64_t k) {
    constexpr int kQK = 32, kBlockBytes = 4 + 4 + kQK / 2;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(q5_1_quantize(xb, blocks), "q5_1_quantize");
}

void ggml_quants_halide_dequantize_q5_1(const void *x, float *y, int64_t k) {
    constexpr int kQK = 32, kBlockBytes = 4 + 4 + kQK / 2;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(q5_1_dequantize(blocks, yb), "q5_1_dequantize");
}

//
// Q8_0 -- block size 32, 34 bytes/block (2 delta + 32 int8 values).
//

void ggml_quants_halide_quantize_q8_0(const float *x, void *y, int64_t k) {
    constexpr int kQK = 32, kBlockBytes = 2 + kQK;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(q8_0_quantize(xb, blocks), "q8_0_quantize");
}

void ggml_quants_halide_dequantize_q8_0(const void *x, float *y, int64_t k) {
    constexpr int kQK = 32, kBlockBytes = 2 + kQK;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(q8_0_dequantize(blocks, yb), "q8_0_dequantize");
}

//
// Q8_1 -- block size 32, 36 bytes/block (2 delta + 2 sum + 32 int8 values).
// Quantize only -- GGML has no public dequantize for this activation-only format.
//

void ggml_quants_halide_quantize_q8_1(const float *x, void *y, int64_t k) {
    constexpr int kQK = 32, kBlockBytes = 4 + kQK;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(q8_1_quantize(xb, blocks), "q8_1_quantize");
}

//
// Q8_K -- superblock size 256, 292 bytes/block (4 float32 delta + 256 int8
// values + 16 int16 bsums). Quantize only -- GGML has no public dequantize
// for this activation-only format.
//

void ggml_quants_halide_quantize_q8_k(const float *x, void *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = 4 + kQK + (kQK / 16) * 2;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(q8_k_quantize(xb, blocks), "q8_k_quantize");
}

//
// Q2_K -- superblock size 256, 84 bytes/block (16 scale/min nibble bytes +
// 64 packed-2-bit bytes + 2 delta + 2 dmin). Dequantize is native Halide;
// quantize calls out to GGML's own reference (see ggml_extern_quantize.cpp).
//

void ggml_quants_halide_quantize_q2_k(const float *x, void *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = 16 + kQK / 4 + 4;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(q2_k_quantize(xb, blocks), "q2_k_quantize");
}

void ggml_quants_halide_dequantize_q2_k(const void *x, float *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = 16 + kQK / 4 + 4;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(q2_k_dequantize(blocks, yb), "q2_k_dequantize");
}

//
// Q6_K -- superblock size 256, 210 bytes/block (128 ql + 64 qh + 16 signed
// int8 scales + 2 delta). Dequantize is native Halide; quantize calls out
// to GGML's own reference (see ggml_extern_quantize.cpp).
//

void ggml_quants_halide_quantize_q6_k(const float *x, void *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = kQK / 2 + kQK / 4 + kQK / 16 + 2;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(q6_k_quantize(xb, blocks), "q6_k_quantize");
}

void ggml_quants_halide_dequantize_q6_k(const void *x, float *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = kQK / 2 + kQK / 4 + kQK / 16 + 2;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(q6_k_dequantize(blocks, yb), "q6_k_dequantize");
}

//
// Q4_K -- superblock size 256, 144 bytes/block (2 delta + 2 dmin + 12 packed
// scale/min bytes + 128 packed-4-bit bytes). Dequantize is native Halide;
// quantize calls out to GGML's own reference (see ggml_extern_quantize.cpp).
//

void ggml_quants_halide_quantize_q4_k(const float *x, void *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = 4 + 12 + kQK / 2;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(q4_k_quantize(xb, blocks), "q4_k_quantize");
}

void ggml_quants_halide_dequantize_q4_k(const void *x, float *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = 4 + 12 + kQK / 2;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(q4_k_dequantize(blocks, yb), "q4_k_dequantize");
}

//
// Q5_K -- superblock size 256, 176 bytes/block (2 delta + 2 dmin + 12 packed
// scale/min bytes + 32 high-bit bytes + 128 packed-4-bit bytes). Dequantize
// is native Halide; quantize calls out to GGML's own reference (see
// ggml_extern_quantize.cpp).
//

void ggml_quants_halide_quantize_q5_k(const float *x, void *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = 4 + 12 + kQK / 8 + kQK / 2;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(q5_k_quantize(xb, blocks), "q5_k_quantize");
}

void ggml_quants_halide_dequantize_q5_k(const void *x, float *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = 4 + 12 + kQK / 8 + kQK / 2;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(q5_k_dequantize(blocks, yb), "q5_k_dequantize");
}

//
// Q3_K -- superblock size 256, 110 bytes/block (32 hmask + 64 packed-2-bit
// bytes + 12 packed scale bytes + 2 delta). Dequantize is native Halide;
// quantize calls out to GGML's own reference (see ggml_extern_quantize.cpp).
//

void ggml_quants_halide_quantize_q3_k(const float *x, void *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = kQK / 8 + kQK / 4 + 12 + 2;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(q3_k_quantize(xb, blocks), "q3_k_quantize");
}

void ggml_quants_halide_dequantize_q3_k(const void *x, float *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = kQK / 8 + kQK / 4 + 12 + 2;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(q3_k_dequantize(blocks, yb), "q3_k_dequantize");
}

//
// Q1_0 -- block size 128, 18 bytes/block (2 delta + 16 sign-bit bytes).
// Closed-form both directions, fully native.
//

void ggml_quants_halide_quantize_q1_0(const float *x, void *y, int64_t k) {
    constexpr int kQK = 128, kBlockBytes = 2 + kQK / 8;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(q1_0_quantize(xb, blocks), "q1_0_quantize");
}

void ggml_quants_halide_dequantize_q1_0(const void *x, float *y, int64_t k) {
    constexpr int kQK = 128, kBlockBytes = 2 + kQK / 8;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(q1_0_dequantize(blocks, yb), "q1_0_dequantize");
}

//
// MXFP4 -- block size 32, 17 bytes/block (1 E8M0 exponent + 16 packed-4-bit
// codebook-index bytes). Dequantize is native Halide; quantize calls out to
// GGML's own reference (see ggml_extern_quantize.cpp).
//

void ggml_quants_halide_quantize_mxfp4(const float *x, void *y, int64_t k) {
    constexpr int kQK = 32, kBlockBytes = 1 + kQK / 2;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(mxfp4_quantize(xb, blocks), "mxfp4_quantize");
}

void ggml_quants_halide_dequantize_mxfp4(const void *x, float *y, int64_t k) {
    constexpr int kQK = 32, kBlockBytes = 1 + kQK / 2;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(mxfp4_dequantize(blocks, yb), "mxfp4_dequantize");
}

//
// NVFP4 -- block size 64, 36 bytes/block (4 UE4M3 scales + 32 packed-4-bit
// codebook-index bytes). Dequantize is native Halide; quantize calls out to
// GGML's own reference (see ggml_extern_quantize.cpp).
//

void ggml_quants_halide_quantize_nvfp4(const float *x, void *y, int64_t k) {
    constexpr int kQK = 64, kSub = 16, kBlockBytes = kQK / kSub + kQK / 2;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(nvfp4_quantize(xb, blocks), "nvfp4_quantize");
}

void ggml_quants_halide_dequantize_nvfp4(const void *x, float *y, int64_t k) {
    constexpr int kQK = 64, kSub = 16, kBlockBytes = kQK / kSub + kQK / 2;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(nvfp4_dequantize(blocks, yb), "nvfp4_dequantize");
}

//
// IQ4_NL -- block size 32, 18 bytes/block (2 delta + 16 packed-4-bit
// codebook-index bytes). Dequantize is native Halide; quantize calls out to
// GGML's own reference (see ggml_extern_quantize.cpp).
//

void ggml_quants_halide_quantize_iq4_nl(const float *x, void *y, int64_t k) {
    constexpr int kQK = 32, kBlockBytes = 2 + kQK / 2;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(iq4_nl_quantize(xb, blocks), "iq4_nl_quantize");
}

void ggml_quants_halide_dequantize_iq4_nl(const void *x, float *y, int64_t k) {
    constexpr int kQK = 32, kBlockBytes = 2 + kQK / 2;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(iq4_nl_dequantize(blocks, yb), "iq4_nl_dequantize");
}

//
// IQ4_XS -- superblock size 256, 136 bytes/block (2 delta + 2 scales_h + 4
// scales_l + 128 packed-4-bit codebook-index bytes). Dequantize is native
// Halide; quantize calls out to GGML's own reference (see
// ggml_extern_quantize.cpp).
//

void ggml_quants_halide_quantize_iq4_xs(const float *x, void *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = 2 + 2 + 4 + kQK / 2;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(iq4_xs_quantize(xb, blocks), "iq4_xs_quantize");
}

void ggml_quants_halide_dequantize_iq4_xs(const void *x, float *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = 2 + 2 + 4 + kQK / 2;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(iq4_xs_dequantize(blocks, yb), "iq4_xs_dequantize");
}

//
// TQ1_0 -- superblock size 256, 54 bytes/block (48 base-3-packed qs + 4
// base-3-packed qh + 2 delta). Dequantize is native Halide; quantize calls
// out to GGML's own reference (see ggml_extern_quantize.cpp).
//

void ggml_quants_halide_quantize_tq1_0(const float *x, void *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = 54;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(tq1_0_quantize(xb, blocks), "tq1_0_quantize");
}

void ggml_quants_halide_dequantize_tq1_0(const void *x, float *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = 54;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(tq1_0_dequantize(blocks, yb), "tq1_0_dequantize");
}

//
// TQ2_0 -- superblock size 256, 66 bytes/block (64 packed-2-bit qs + 2
// delta -- qs before d, unlike every other type here). Dequantize is native
// Halide; quantize calls out to GGML's own reference (see
// ggml_extern_quantize.cpp).
//

void ggml_quants_halide_quantize_tq2_0(const float *x, void *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = kQK / 4 + 2;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(tq2_0_quantize(xb, blocks), "tq2_0_quantize");
}

void ggml_quants_halide_dequantize_tq2_0(const void *x, float *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = kQK / 4 + 2;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(tq2_0_dequantize(blocks, yb), "tq2_0_dequantize");
}

//
// IQ2_XXS -- superblock size 256, 66 bytes/block (2 delta + 64 packed qs).
// Dequantize only -- see ggml_quants.h for why there's no quantize here.
//

void ggml_quants_halide_dequantize_iq2_xxs(const void *x, float *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = 2 + kQK / 8 * 2;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(iq2_xxs_dequantize(blocks, yb), "iq2_xxs_dequantize");
}

//
// IQ2_XS -- superblock size 256, 74 bytes/block. Dequantize only.
//

void ggml_quants_halide_dequantize_iq2_xs(const void *x, float *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = 2 + kQK / 8 * 2 + kQK / 32;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(iq2_xs_dequantize(blocks, yb), "iq2_xs_dequantize");
}

//
// IQ2_S -- superblock size 256, 82 bytes/block. Dequantize is native
// Halide; quantize calls out to GGML's own reference.
//

void ggml_quants_halide_quantize_iq2_s(const float *x, void *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = 2 + kQK / 8 + kQK / 8 + kQK / 32 + kQK / 32;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(iq2_s_quantize(xb, blocks), "iq2_s_quantize");
}

void ggml_quants_halide_dequantize_iq2_s(const void *x, float *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = 2 + kQK / 8 + kQK / 8 + kQK / 32 + kQK / 32;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(iq2_s_dequantize(blocks, yb), "iq2_s_dequantize");
}

//
// IQ3_XXS -- superblock size 256, 98 bytes/block. Dequantize is native
// Halide; quantize calls out to GGML's own reference.
//

void ggml_quants_halide_quantize_iq3_xxs(const float *x, void *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = 2 + 3 * kQK / 8;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(iq3_xxs_quantize(xb, blocks), "iq3_xxs_quantize");
}

void ggml_quants_halide_dequantize_iq3_xxs(const void *x, float *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = 2 + 3 * kQK / 8;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(iq3_xxs_dequantize(blocks, yb), "iq3_xxs_dequantize");
}

//
// IQ3_S -- superblock size 256, 110 bytes/block. Dequantize is native
// Halide; quantize calls out to GGML's own reference.
//

void ggml_quants_halide_quantize_iq3_s(const float *x, void *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = 2 + kQK / 4 + kQK / 32 + kQK / 8 + kQK / 64;
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(static_cast<uint8_t *>(y), 2, shape);
    check(iq3_s_quantize(xb, blocks), "iq3_s_quantize");
}

void ggml_quants_halide_dequantize_iq3_s(const void *x, float *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = 2 + kQK / 4 + kQK / 32 + kQK / 8 + kQK / 64;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(iq3_s_dequantize(blocks, yb), "iq3_s_dequantize");
}

//
// IQ1_S -- superblock size 256, 50 bytes/block. Dequantize only.
//

void ggml_quants_halide_dequantize_iq1_s(const void *x, float *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = 2 + kQK / 8 + kQK / 16;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(iq1_s_dequantize(blocks, yb), "iq1_s_dequantize");
}

//
// IQ1_M -- superblock size 256, 56 bytes/block. Dequantize only.
//

void ggml_quants_halide_dequantize_iq1_m(const void *x, float *y, int64_t k) {
    constexpr int kQK = 256, kBlockBytes = kQK / 8 + kQK / 16 + kQK / 32;
    const int32_t nb = static_cast<int32_t>(k / kQK);
    halide_dimension_t shape[2] = {{0, kBlockBytes, 1}, {0, nb, kBlockBytes}};
    Buffer<uint8_t, 2> blocks(const_cast<uint8_t *>(static_cast<const uint8_t *>(x)), 2, shape);
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(iq1_m_dequantize(blocks, yb), "iq1_m_dequantize");
}

//
// F16 -- block size 1, 2 bytes/element (plain IEEE binary16 cast, no header).
//

void ggml_quants_halide_quantize_f16(const float *x, void *y, int64_t k) {
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    Buffer<uint16_t, 1> yb(static_cast<uint16_t *>(y), static_cast<int>(k));
    check(f16_quantize(xb, yb), "f16_quantize");
}

void ggml_quants_halide_dequantize_f16(const void *x, float *y, int64_t k) {
    Buffer<uint16_t, 1> xb(const_cast<uint16_t *>(static_cast<const uint16_t *>(x)), static_cast<int>(k));
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(f16_dequantize(xb, yb), "f16_dequantize");
}

//
// BF16 -- block size 1, 2 bytes/element (plain bfloat16 cast, no header).
//

void ggml_quants_halide_quantize_bf16(const float *x, void *y, int64_t k) {
    Buffer<float, 1> xb(const_cast<float *>(x), static_cast<int>(k));
    Buffer<uint16_t, 1> yb(static_cast<uint16_t *>(y), static_cast<int>(k));
    check(bf16_quantize(xb, yb), "bf16_quantize");
}

void ggml_quants_halide_dequantize_bf16(const void *x, float *y, int64_t k) {
    Buffer<uint16_t, 1> xb(const_cast<uint16_t *>(static_cast<const uint16_t *>(x)), static_cast<int>(k));
    Buffer<float, 1> yb(y, static_cast<int>(k));
    check(bf16_dequantize(xb, yb), "bf16_dequantize");
}

}  // extern "C"
