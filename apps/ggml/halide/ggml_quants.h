#pragma once

// Plain C ABI for the Halide-generated quantize/dequantize kernels, matching
// apps/ggml/include/kernel_registry.h's quantize_fn_t/dequantize_fn_t
// signatures exactly (by signature compatibility alone -- no shared header
// needed between this library and the benchmark harness).
//
// Q8_1 has no dequantize entry: it's an activation-only format (GGML itself
// has no public to_float for it), so there is nothing to implement.

#include <cstdint>

extern "C" {

void ggml_quants_halide_quantize_q4_0(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q4_0(const void *x, float *y, int64_t k);

void ggml_quants_halide_quantize_q4_1(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q4_1(const void *x, float *y, int64_t k);

void ggml_quants_halide_quantize_q5_0(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q5_0(const void *x, float *y, int64_t k);

void ggml_quants_halide_quantize_q5_1(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q5_1(const void *x, float *y, int64_t k);

void ggml_quants_halide_quantize_q8_0(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q8_0(const void *x, float *y, int64_t k);

void ggml_quants_halide_quantize_q8_1(const float *x, void *y, int64_t k);

void ggml_quants_halide_quantize_q8_k(const float *x, void *y, int64_t k);

// Q2_K, Q6_K: dequantize is a from-scratch Halide implementation; quantize
// is scaffolding that calls out to GGML's own reference (see
// ggml_extern_quantize.cpp) pending a from-scratch port of GGML's iterative
// scale search.
void ggml_quants_halide_quantize_q2_k(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q2_k(const void *x, float *y, int64_t k);

void ggml_quants_halide_quantize_q6_k(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q6_k(const void *x, float *y, int64_t k);

void ggml_quants_halide_quantize_q4_k(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q4_k(const void *x, float *y, int64_t k);

void ggml_quants_halide_quantize_q5_k(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q5_k(const void *x, float *y, int64_t k);

void ggml_quants_halide_quantize_q3_k(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q3_k(const void *x, float *y, int64_t k);

// Q1_0: closed-form both directions, fully native.
void ggml_quants_halide_quantize_q1_0(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q1_0(const void *x, float *y, int64_t k);

// MXFP4, NVFP4, IQ4_NL, IQ4_XS, TQ1_0, TQ2_0: dequantize is native Halide;
// quantize calls out to GGML's own reference (see ggml_extern_quantize.cpp).
void ggml_quants_halide_quantize_mxfp4(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_mxfp4(const void *x, float *y, int64_t k);

void ggml_quants_halide_quantize_nvfp4(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_nvfp4(const void *x, float *y, int64_t k);

void ggml_quants_halide_quantize_iq4_nl(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_iq4_nl(const void *x, float *y, int64_t k);

void ggml_quants_halide_quantize_iq4_xs(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_iq4_xs(const void *x, float *y, int64_t k);

void ggml_quants_halide_quantize_tq1_0(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_tq1_0(const void *x, float *y, int64_t k);

void ggml_quants_halide_quantize_tq2_0(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_tq2_0(const void *x, float *y, int64_t k);

// IQ2_XXS: dequantize only. GGML has no public from_float_ref for this
// importance-matrix-only codebook type (only a private whole-matrix
// quantizer -- see providers/ggml_internal_abi.h), so there is no
// from-scratch quantizer to write against it.
void ggml_quants_halide_dequantize_iq2_xxs(const void *x, float *y, int64_t k);

// IQ2_XS: dequantize only (same reason as IQ2_XXS above).
void ggml_quants_halide_dequantize_iq2_xs(const void *x, float *y, int64_t k);

// IQ2_S, IQ3_XXS, IQ3_S: dequantize is native Halide; quantize calls out to
// GGML's own reference (see ggml_extern_quantize.cpp) -- these three do
// have a public from_float_ref, unlike IQ2_XXS/IQ2_XS/IQ1_S/IQ1_M.
void ggml_quants_halide_quantize_iq2_s(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_iq2_s(const void *x, float *y, int64_t k);

void ggml_quants_halide_quantize_iq3_xxs(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_iq3_xxs(const void *x, float *y, int64_t k);

void ggml_quants_halide_quantize_iq3_s(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_iq3_s(const void *x, float *y, int64_t k);

// IQ1_S, IQ1_M: dequantize only (same reason as IQ2_XXS above).
void ggml_quants_halide_dequantize_iq1_s(const void *x, float *y, int64_t k);
void ggml_quants_halide_dequantize_iq1_m(const void *x, float *y, int64_t k);

// F16, BF16: not really "quantized" types -- block size 1, a plain per-
// element float cast. Both directions are fully native Halide (Halide's
// built-in float16_t/bfloat16_t casts implement the same IEEE round-to-
// nearest-even conversions GGML's own reference uses).
void ggml_quants_halide_quantize_f16(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_f16(const void *x, float *y, int64_t k);

void ggml_quants_halide_quantize_bf16(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_bf16(const void *x, float *y, int64_t k);
}
