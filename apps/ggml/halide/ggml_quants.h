#pragma once

// Plain C ABI for the Halide-generated quantize/dequantize/vec_dot kernels,
// matching apps/ggml/include/kernel_registry.h's quantize_fn_t/
// dequantize_fn_t/vec_dot_fn_t signatures exactly (by signature
// compatibility alone -- no shared header needed between this library and
// the benchmark harness).
//
// Q8_1 has no dequantize entry: it's an activation-only format (GGML itself
// has no public to_float for it), so there is nothing to implement.
//
// Every vec_dot_<x>_<y> function computes the dot product between a row of
// weight-type x and a row of activation-type y (y is x's GGML vec_dot_type),
// via a from-scratch Halide reimplementation -- see each type's
// *_generators.cpp VecDot generator, and activation_dequant.h for the
// shared Q8_0/Q8_1/Q8_K activation-side helpers they all call.

#include <cstddef>
#include <cstdint>

extern "C" {

void ggml_quants_halide_quantize_q4_0(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q4_0(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_q4_0_q8_0(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                          size_t by, int nrc);

void ggml_quants_halide_quantize_q4_1(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q4_1(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_q4_1_q8_1(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                          size_t by, int nrc);

void ggml_quants_halide_quantize_q5_0(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q5_0(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_q5_0_q8_0(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                          size_t by, int nrc);

void ggml_quants_halide_quantize_q5_1(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q5_1(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_q5_1_q8_1(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                          size_t by, int nrc);

void ggml_quants_halide_quantize_q8_0(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q8_0(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_q8_0_q8_0(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                          size_t by, int nrc);

void ggml_quants_halide_quantize_q8_1(const float *x, void *y, int64_t k);

void ggml_quants_halide_quantize_q8_k(const float *x, void *y, int64_t k);

// Q2_K, Q6_K: dequantize is a from-scratch Halide implementation; quantize
// is scaffolding that calls out to GGML's own reference (see
// ggml_extern_quantize.cpp) pending a from-scratch port of GGML's iterative
// scale search. vec_dot is from-scratch (against Q8_K activations).
void ggml_quants_halide_quantize_q2_k(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q2_k(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_q2_k_q8_k(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                          size_t by, int nrc);

void ggml_quants_halide_quantize_q6_k(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q6_k(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_q6_k_q8_k(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                          size_t by, int nrc);

void ggml_quants_halide_quantize_q4_k(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q4_k(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_q4_k_q8_k(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                          size_t by, int nrc);

void ggml_quants_halide_quantize_q5_k(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q5_k(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_q5_k_q8_k(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                          size_t by, int nrc);

void ggml_quants_halide_quantize_q3_k(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q3_k(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_q3_k_q8_k(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                          size_t by, int nrc);

// Q1_0: closed-form both directions, fully native.
void ggml_quants_halide_quantize_q1_0(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_q1_0(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_q1_0_q8_0(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                          size_t by, int nrc);

// MXFP4, NVFP4, IQ4_NL, IQ4_XS, TQ1_0, TQ2_0: dequantize is native Halide;
// quantize calls out to GGML's own reference (see ggml_extern_quantize.cpp).
void ggml_quants_halide_quantize_mxfp4(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_mxfp4(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_mxfp4_q8_0(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                           size_t by, int nrc);

void ggml_quants_halide_quantize_nvfp4(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_nvfp4(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_nvfp4_q8_0(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                           size_t by, int nrc);

void ggml_quants_halide_quantize_iq4_nl(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_iq4_nl(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_iq4_nl_q8_0(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                            size_t by, int nrc);

void ggml_quants_halide_quantize_iq4_xs(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_iq4_xs(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_iq4_xs_q8_k(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                            size_t by, int nrc);

void ggml_quants_halide_quantize_tq1_0(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_tq1_0(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_tq1_0_q8_k(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                           size_t by, int nrc);

void ggml_quants_halide_quantize_tq2_0(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_tq2_0(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_tq2_0_q8_k(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                           size_t by, int nrc);

// IQ2_XXS: dequantize only. GGML has no public from_float_ref for this
// importance-matrix-only codebook type (only a private whole-matrix
// quantizer -- see providers/ggml_internal_abi.h), so there is no
// from-scratch quantizer to write against it. vec_dot is still implemented
// (GGML's own reference quantizer is used to produce test/benchmark input).
void ggml_quants_halide_dequantize_iq2_xxs(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_iq2_xxs_q8_k(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                             size_t by, int nrc);

// IQ2_XS: dequantize only (same reason as IQ2_XXS above).
void ggml_quants_halide_dequantize_iq2_xs(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_iq2_xs_q8_k(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                            size_t by, int nrc);

// IQ2_S, IQ3_XXS, IQ3_S: dequantize is native Halide; quantize calls out to
// GGML's own reference (see ggml_extern_quantize.cpp) -- these three do
// have a public from_float_ref, unlike IQ2_XXS/IQ2_XS/IQ1_S/IQ1_M.
void ggml_quants_halide_quantize_iq2_s(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_iq2_s(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_iq2_s_q8_k(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                           size_t by, int nrc);

void ggml_quants_halide_quantize_iq3_xxs(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_iq3_xxs(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_iq3_xxs_q8_k(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                             size_t by, int nrc);

void ggml_quants_halide_quantize_iq3_s(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_iq3_s(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_iq3_s_q8_k(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                           size_t by, int nrc);

// IQ1_S, IQ1_M: dequantize only (same reason as IQ2_XXS above).
void ggml_quants_halide_dequantize_iq1_s(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_iq1_s_q8_k(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                           size_t by, int nrc);

void ggml_quants_halide_dequantize_iq1_m(const void *x, float *y, int64_t k);
void ggml_quants_halide_vec_dot_iq1_m_q8_k(int n, float *s, size_t bs, const void *vx, size_t bx, const void *vy,
                                           size_t by, int nrc);

// F16, BF16: not really "quantized" types -- block size 1, a plain per-
// element float cast. Both directions are fully native Halide (Halide's
// built-in float16_t/bfloat16_t casts implement the same IEEE round-to-
// nearest-even conversions GGML's own reference uses). No vec_dot: not part
// of the quantized-format vec_dot sweep this directory otherwise covers.
void ggml_quants_halide_quantize_f16(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_f16(const void *x, float *y, int64_t k);

void ggml_quants_halide_quantize_bf16(const float *x, void *y, int64_t k);
void ggml_quants_halide_dequantize_bf16(const void *x, float *y, int64_t k);
}
