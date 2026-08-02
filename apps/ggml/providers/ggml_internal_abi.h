#pragma once

// PRIVATE, VERSION-PINNED ABI SURFACE -- READ BEFORE TOUCHING
//
// The functions declared below are internal implementation details of
// ggml-cpu (declared in the *uninstalled* headers src/ggml-cpu/quants.h and
// src/ggml-cpu/repack.h). GGML does not install those headers, does not
// document these symbols, does not version them, and offers no ABI
// stability guarantee for them whatsoever.
//
// They are reachable from an external application ONLY because ggml-cpu is
// built without -fvisibility=hidden: every plain, non-static C function ends
// up with default (exported) linker visibility by accident of the build
// configuration, not by design. This header is a hand-copied snapshot of
// the declarations in ggml (as of the commit this file was written against;
// see README.md) -- if a future GGML release renames, removes, or changes
// the signature of one of these functions, this header (and only this
// header + ggml_provider.cpp) will need updating. No other part of
// kernel-bench depends on GGML internals.
//
// Why we need this at all: GGML's public API (ggml_get_type_traits /
// ggml_get_type_traits_cpu, see include/ggml.h and include/ggml-cpu.h)
// exposes exactly one "reference" and one "dispatched" implementation per
// type for quantize/dequantize, which is enough for those two categories
// without touching anything private (see ggml_provider.cpp). It does NOT
// expose the always-available pure-C fallback for vec_dot or for the repack
// quantize_mat/gemv/gemm kernels -- the only way to reach those, and thus
// the only way to compare them against the (possibly arch-optimized)
// canonical symbol, is by declaring both names ourselves and letting the
// linker resolve them.
//
// IMPORTANT -- the `_generic` name does not always exist as its own link
// symbol: src/ggml-cpu/arch-fallback.h #defines it onto the canonical name
// -- as a textual macro substitution inside GGML's own .c/.cpp files -- for
// whichever functions the current architecture has no distinct optimized
// version of, and there is then only one function in the binary. (A weak
// C++ declaration doesn't paper over this: verified empirically that
// Darwin's ld64 hard-fails on an undefined `weak_import` symbol that has
// zero definitions anywhere in the link, and GNU ld's "resolve undefined
// weak to null" behavior isn't something to rely on portably either.) So
// this header mirrors arch-fallback.h's own collapsing, using the same
// preprocessor guards, restricted to the subset of functions declared
// below. When a `_generic` name collapses onto its canonical counterpart
// here exactly as it does inside GGML itself, ggml_provider.cpp's
// pointer-equality check (`generic_fn == canonical_fn`) naturally detects
// "single implementation, nothing to compare" for that kernel on this
// architecture. If GGML's own arch-fallback.h changes its collapsing list,
// this block needs updating to match; this is the one part of this file
// most likely to need attention when moving to a newer GGML.
#if defined(__aarch64__) || defined(__arm__) || defined(_M_ARM) || defined(_M_ARM64)
#define ggml_quantize_mat_q8_K_4x4_generic ggml_quantize_mat_q8_K_4x4
#define ggml_quantize_mat_q8_K_4x8_generic ggml_quantize_mat_q8_K_4x8
#define ggml_gemv_iq4_nl_8x8_q8_0_generic ggml_gemv_iq4_nl_8x8_q8_0
#define ggml_gemv_mxfp4_8x8_q8_0_generic ggml_gemv_mxfp4_8x8_q8_0
#define ggml_gemv_q2_K_8x8_q8_K_generic ggml_gemv_q2_K_8x8_q8_K
#define ggml_gemm_iq4_nl_8x8_q8_0_generic ggml_gemm_iq4_nl_8x8_q8_0
#define ggml_gemm_mxfp4_8x8_q8_0_generic ggml_gemm_mxfp4_8x8_q8_0
#define ggml_gemm_q2_K_8x8_q8_K_generic ggml_gemm_q2_K_8x8_q8_K
#elif defined(__x86_64__) || defined(__i386__) || defined(_M_IX86) || defined(_M_X64)
#define ggml_vec_dot_nvfp4_q8_0_generic ggml_vec_dot_nvfp4_q8_0
#define ggml_quantize_mat_q8_0_4x4_generic ggml_quantize_mat_q8_0_4x4
#define ggml_quantize_mat_q8_K_4x4_generic ggml_quantize_mat_q8_K_4x4
#define ggml_gemv_q4_0_4x4_q8_0_generic ggml_gemv_q4_0_4x4_q8_0
#define ggml_gemv_q4_0_4x8_q8_0_generic ggml_gemv_q4_0_4x8_q8_0
#define ggml_gemv_q4_K_8x4_q8_K_generic ggml_gemv_q4_K_8x4_q8_K
#define ggml_gemv_q5_K_8x4_q8_K_generic ggml_gemv_q5_K_8x4_q8_K
#define ggml_gemv_q5_K_8x8_q8_K_generic ggml_gemv_q5_K_8x8_q8_K
#define ggml_gemv_q6_K_8x4_q8_K_generic ggml_gemv_q6_K_8x4_q8_K
#define ggml_gemv_q6_K_8x8_q8_K_generic ggml_gemv_q6_K_8x8_q8_K
#define ggml_gemv_iq4_nl_4x4_q8_0_generic ggml_gemv_iq4_nl_4x4_q8_0
#define ggml_gemv_mxfp4_4x4_q8_0_generic ggml_gemv_mxfp4_4x4_q8_0
#define ggml_gemv_q8_0_4x4_q8_0_generic ggml_gemv_q8_0_4x4_q8_0
#define ggml_gemv_q8_0_4x8_q8_0_generic ggml_gemv_q8_0_4x8_q8_0
#define ggml_gemm_q4_0_4x4_q8_0_generic ggml_gemm_q4_0_4x4_q8_0
#define ggml_gemm_q4_0_4x8_q8_0_generic ggml_gemm_q4_0_4x8_q8_0
#define ggml_gemm_q4_K_8x4_q8_K_generic ggml_gemm_q4_K_8x4_q8_K
#define ggml_gemm_q5_K_8x4_q8_K_generic ggml_gemm_q5_K_8x4_q8_K
#define ggml_gemm_q5_K_8x8_q8_K_generic ggml_gemm_q5_K_8x8_q8_K
#define ggml_gemm_q6_K_8x4_q8_K_generic ggml_gemm_q6_K_8x4_q8_K
#define ggml_gemm_q6_K_8x8_q8_K_generic ggml_gemm_q6_K_8x8_q8_K
#define ggml_gemm_iq4_nl_4x4_q8_0_generic ggml_gemm_iq4_nl_4x4_q8_0
#define ggml_gemm_mxfp4_4x4_q8_0_generic ggml_gemm_mxfp4_4x4_q8_0
#define ggml_gemm_q8_0_4x4_q8_0_generic ggml_gemm_q8_0_4x4_q8_0
#define ggml_gemm_q8_0_4x8_q8_0_generic ggml_gemm_q8_0_4x8_q8_0
#elif defined(__POWERPC__) || defined(__powerpc__) || defined(__loongarch64) || defined(__riscv) || \
    defined(__s390x__) || defined(__wasm__)
// PowerPC/LoongArch/RISC-V/s390x/wasm each collapse a large, differently-shaped
// subset of quants.c/repack.cpp symbols (see arch-fallback.h) -- rather than
// transcribe five more per-arch lists by hand, collapse everything this
// header declares on these architectures. This is conservative in the safe
// direction: on an arch that actually kept a real optimized/generic split
// for some function, this makes that pairing look like "single
// implementation" instead of reporting it, but it will never misreport two
// genuinely different implementations as identical, and it will never
// produce a link error.
#define quantize_row_q8_K_generic quantize_row_q8_K
#define ggml_vec_dot_q1_0_q8_0_generic ggml_vec_dot_q1_0_q8_0
#define ggml_vec_dot_mxfp4_q8_0_generic ggml_vec_dot_mxfp4_q8_0
#define ggml_vec_dot_nvfp4_q8_0_generic ggml_vec_dot_nvfp4_q8_0
#define ggml_vec_dot_tq1_0_q8_K_generic ggml_vec_dot_tq1_0_q8_K
#define ggml_vec_dot_tq2_0_q8_K_generic ggml_vec_dot_tq2_0_q8_K
#define ggml_vec_dot_q2_K_q8_K_generic ggml_vec_dot_q2_K_q8_K
#define ggml_vec_dot_iq2_xxs_q8_K_generic ggml_vec_dot_iq2_xxs_q8_K
#define ggml_vec_dot_iq2_xs_q8_K_generic ggml_vec_dot_iq2_xs_q8_K
#define ggml_vec_dot_iq2_s_q8_K_generic ggml_vec_dot_iq2_s_q8_K
#define ggml_vec_dot_iq3_xxs_q8_K_generic ggml_vec_dot_iq3_xxs_q8_K
#define ggml_vec_dot_iq3_s_q8_K_generic ggml_vec_dot_iq3_s_q8_K
#define ggml_vec_dot_iq1_s_q8_K_generic ggml_vec_dot_iq1_s_q8_K
#define ggml_vec_dot_iq1_m_q8_K_generic ggml_vec_dot_iq1_m_q8_K
#define ggml_vec_dot_iq4_nl_q8_0_generic ggml_vec_dot_iq4_nl_q8_0
#define ggml_vec_dot_iq4_xs_q8_K_generic ggml_vec_dot_iq4_xs_q8_K
#define ggml_quantize_mat_q8_0_4x4_generic ggml_quantize_mat_q8_0_4x4
#define ggml_quantize_mat_q8_0_4x8_generic ggml_quantize_mat_q8_0_4x8
#define ggml_quantize_mat_q8_K_4x4_generic ggml_quantize_mat_q8_K_4x4
#define ggml_quantize_mat_q8_K_4x8_generic ggml_quantize_mat_q8_K_4x8
#define ggml_gemv_q4_0_4x4_q8_0_generic ggml_gemv_q4_0_4x4_q8_0
#define ggml_gemv_q4_0_4x8_q8_0_generic ggml_gemv_q4_0_4x8_q8_0
#define ggml_gemv_q4_0_8x8_q8_0_generic ggml_gemv_q4_0_8x8_q8_0
#define ggml_gemv_q2_K_8x8_q8_K_generic ggml_gemv_q2_K_8x8_q8_K
#define ggml_gemv_q4_K_8x4_q8_K_generic ggml_gemv_q4_K_8x4_q8_K
#define ggml_gemv_q4_K_8x8_q8_K_generic ggml_gemv_q4_K_8x8_q8_K
#define ggml_gemv_q5_K_8x4_q8_K_generic ggml_gemv_q5_K_8x4_q8_K
#define ggml_gemv_q5_K_8x8_q8_K_generic ggml_gemv_q5_K_8x8_q8_K
#define ggml_gemv_q6_K_8x4_q8_K_generic ggml_gemv_q6_K_8x4_q8_K
#define ggml_gemv_q6_K_8x8_q8_K_generic ggml_gemv_q6_K_8x8_q8_K
#define ggml_gemv_iq4_nl_4x4_q8_0_generic ggml_gemv_iq4_nl_4x4_q8_0
#define ggml_gemv_iq4_nl_8x8_q8_0_generic ggml_gemv_iq4_nl_8x8_q8_0
#define ggml_gemv_mxfp4_4x4_q8_0_generic ggml_gemv_mxfp4_4x4_q8_0
#define ggml_gemv_mxfp4_8x8_q8_0_generic ggml_gemv_mxfp4_8x8_q8_0
#define ggml_gemv_q8_0_4x4_q8_0_generic ggml_gemv_q8_0_4x4_q8_0
#define ggml_gemv_q8_0_4x8_q8_0_generic ggml_gemv_q8_0_4x8_q8_0
#define ggml_gemm_q4_0_4x4_q8_0_generic ggml_gemm_q4_0_4x4_q8_0
#define ggml_gemm_q4_0_4x8_q8_0_generic ggml_gemm_q4_0_4x8_q8_0
#define ggml_gemm_q4_0_8x8_q8_0_generic ggml_gemm_q4_0_8x8_q8_0
#define ggml_gemm_q2_K_8x8_q8_K_generic ggml_gemm_q2_K_8x8_q8_K
#define ggml_gemm_q4_K_8x4_q8_K_generic ggml_gemm_q4_K_8x4_q8_K
#define ggml_gemm_q4_K_8x8_q8_K_generic ggml_gemm_q4_K_8x8_q8_K
#define ggml_gemm_q5_K_8x4_q8_K_generic ggml_gemm_q5_K_8x4_q8_K
#define ggml_gemm_q5_K_8x8_q8_K_generic ggml_gemm_q5_K_8x8_q8_K
#define ggml_gemm_q6_K_8x4_q8_K_generic ggml_gemm_q6_K_8x4_q8_K
#define ggml_gemm_q6_K_8x8_q8_K_generic ggml_gemm_q6_K_8x8_q8_K
#define ggml_gemm_iq4_nl_4x4_q8_0_generic ggml_gemm_iq4_nl_4x4_q8_0
#define ggml_gemm_iq4_nl_8x8_q8_0_generic ggml_gemm_iq4_nl_8x8_q8_0
#define ggml_gemm_mxfp4_4x4_q8_0_generic ggml_gemm_mxfp4_4x4_q8_0
#define ggml_gemm_mxfp4_8x8_q8_0_generic ggml_gemm_mxfp4_8x8_q8_0
#define ggml_gemm_q8_0_4x4_q8_0_generic ggml_gemm_q8_0_4x4_q8_0
#define ggml_gemm_q8_0_4x8_q8_0_generic ggml_gemm_q8_0_4x8_q8_0
#endif

#include <cstddef>
#include <cstdint>

#include <ggml-backend.h>  // ggml_backend_buffer_type_t
#include <ggml.h>          // GGML_RESTRICT

// NOTE: declared with ordinary C++ (mangled) linkage, matching the real
// src/ggml-cpu/repack.h -- this one declaration sits *before* that header's
// `extern "C" { ... }` block, unlike every quantize_row_*/vec_dot_*/gemv/gemm
// declaration below.
ggml_backend_buffer_type_t ggml_backend_cpu_repack_buffer_type(void);

extern "C" {

// -- src/ggml-cpu/quants.h: pure-C reference quantizer for Q8_K. Unlike
// every other quantized type, GGML_TYPE_Q8_K has no `from_float_ref` in the
// public ggml_get_type_traits() table (src/ggml.c) at all -- Q8_K is purely
// an internal activation format for K-quant vec_dot/gemv/gemm, never a
// row-conversion target -- so it needs this private symbol as its
// reference; see the special case in ggml_provider.cpp.
void quantize_row_q8_K_generic(const float *GGML_RESTRICT x, void *GGML_RESTRICT y, int64_t k);

// -- src/ggml-quants.h: whole-matrix quantizers for the importance-matrix-
// only codebook types (IQ2_XXS, IQ2_XS, IQ1_S, IQ1_M). Unlike every other
// quantized type, these have no `from_float_ref` in the public
// ggml_get_type_traits() table at all -- GGML only exposes them through
// this differently-shaped `(src, dst, nrows, n_per_row, imatrix)` signature
// (nrows/n_per_row instead of a flat element count k, and an optional
// importance-matrix pointer), used only by the model-quantization tool.
// Called here with nrows=1, n_per_row=k, imatrix=nullptr to get a plain
// per-row reference, matching the shape every other type's from_float_ref
// already has; see the special case in ggml_provider.cpp. Declared to
// return size_t per GGML's real signature (the number of bytes written).
size_t quantize_iq2_xxs(const float *GGML_RESTRICT src, void *GGML_RESTRICT dst, int64_t nrows, int64_t n_per_row, const float *GGML_RESTRICT imatrix);
size_t quantize_iq2_xs(const float *GGML_RESTRICT src, void *GGML_RESTRICT dst, int64_t nrows, int64_t n_per_row, const float *GGML_RESTRICT imatrix);
size_t quantize_iq1_s(const float *GGML_RESTRICT src, void *GGML_RESTRICT dst, int64_t nrows, int64_t n_per_row, const float *GGML_RESTRICT imatrix);
size_t quantize_iq1_m(const float *GGML_RESTRICT src, void *GGML_RESTRICT dst, int64_t nrows, int64_t n_per_row, const float *GGML_RESTRICT imatrix);

// -- src/ggml-cpu/quants.h: vec_dot, pure-C reference (collapsed onto the
// canonical, arch-dispatched symbol above on architectures with no distinct
// optimized implementation for that type) --
void ggml_vec_dot_q1_0_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_q4_0_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_q4_1_q8_1_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_q5_0_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_q5_1_q8_1_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_q8_0_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_mxfp4_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_nvfp4_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_tq1_0_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_tq2_0_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_q2_K_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_q3_K_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_q4_K_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_q5_K_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_q6_K_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_iq2_xxs_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_iq2_xs_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_iq2_s_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_iq3_xxs_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_iq3_s_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_iq1_s_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_iq1_m_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_iq4_nl_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);
void ggml_vec_dot_iq4_xs_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx, const void *GGML_RESTRICT vy, size_t by, int nrc);

// -- src/ggml-cpu/repack.h: activation packing (float -> interleaved q8 blocks), canonical + reference --
void ggml_quantize_mat_q8_0_4x4(const float *GGML_RESTRICT x, void *GGML_RESTRICT vy, int64_t k);
void ggml_quantize_mat_q8_0_4x8(const float *GGML_RESTRICT x, void *GGML_RESTRICT vy, int64_t k);
void ggml_quantize_mat_q8_K_4x4(const float *GGML_RESTRICT x, void *GGML_RESTRICT vy, int64_t k);
void ggml_quantize_mat_q8_K_4x8(const float *GGML_RESTRICT x, void *GGML_RESTRICT vy, int64_t k);
void ggml_quantize_mat_q8_0_4x4_generic(const float *GGML_RESTRICT x, void *GGML_RESTRICT vy, int64_t k);
void ggml_quantize_mat_q8_0_4x8_generic(const float *GGML_RESTRICT x, void *GGML_RESTRICT vy, int64_t k);
void ggml_quantize_mat_q8_K_4x4_generic(const float *GGML_RESTRICT x, void *GGML_RESTRICT vy, int64_t k);
void ggml_quantize_mat_q8_K_4x8_generic(const float *GGML_RESTRICT x, void *GGML_RESTRICT vy, int64_t k);

// -- src/ggml-cpu/repack.h: gemv/gemm over packed weight blocks, canonical + reference --
void ggml_gemv_q4_0_4x4_q8_0(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q4_0_4x8_q8_0(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q4_0_8x8_q8_0(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q2_K_8x8_q8_K(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q4_K_8x4_q8_K(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q4_K_8x8_q8_K(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q5_K_8x4_q8_K(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q5_K_8x8_q8_K(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q6_K_8x4_q8_K(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q6_K_8x8_q8_K(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_iq4_nl_4x4_q8_0(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_iq4_nl_8x8_q8_0(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_mxfp4_4x4_q8_0(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_mxfp4_8x8_q8_0(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q8_0_4x4_q8_0(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q8_0_4x8_q8_0(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);

void ggml_gemm_q4_0_4x4_q8_0(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q4_0_4x8_q8_0(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q4_0_8x8_q8_0(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q2_K_8x8_q8_K(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q4_K_8x4_q8_K(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q4_K_8x8_q8_K(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q5_K_8x4_q8_K(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q5_K_8x8_q8_K(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q6_K_8x4_q8_K(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q6_K_8x8_q8_K(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_iq4_nl_4x4_q8_0(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_iq4_nl_8x8_q8_0(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_mxfp4_4x4_q8_0(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_mxfp4_8x8_q8_0(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q8_0_4x4_q8_0(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q8_0_4x8_q8_0(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);

void ggml_gemv_q4_0_4x4_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q4_0_4x8_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q4_0_8x8_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q2_K_8x8_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q4_K_8x4_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q4_K_8x8_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q5_K_8x4_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q5_K_8x8_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q6_K_8x4_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q6_K_8x8_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_iq4_nl_4x4_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_iq4_nl_8x8_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_mxfp4_4x4_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_mxfp4_8x8_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q8_0_4x4_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemv_q8_0_4x8_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);

void ggml_gemm_q4_0_4x4_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q4_0_4x8_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q4_0_8x8_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q2_K_8x8_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q4_K_8x4_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q4_K_8x8_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q5_K_8x4_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q5_K_8x8_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q6_K_8x4_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q6_K_8x8_q8_K_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_iq4_nl_4x4_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_iq4_nl_8x8_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_mxfp4_4x4_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_mxfp4_8x8_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q8_0_4x4_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);
void ggml_gemm_q8_0_4x8_q8_0_generic(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, const void *GGML_RESTRICT vy, int nr, int nc);

}  // extern "C"
