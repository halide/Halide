#include "ggml_provider.h"
#include "ggml_internal_abi.h"

#include <ggml-cpu.h>
#include <ggml.h>

#include <vector>

namespace {

// type -> pure-C reference vec_dot (src/ggml-cpu/quants.h `_generic` symbols).
// The canonical (possibly arch-optimized) candidate is obtained separately,
// through the PUBLIC ggml_get_type_traits_cpu(type)->vec_dot.
struct VecDotRef {
    ggml_type type;
    vec_dot_fn_t fn;
};

const VecDotRef k_vec_dot_refs[] = {
    {GGML_TYPE_Q1_0, ggml_vec_dot_q1_0_q8_0_generic},
    {GGML_TYPE_Q4_0, ggml_vec_dot_q4_0_q8_0_generic},
    {GGML_TYPE_Q4_1, ggml_vec_dot_q4_1_q8_1_generic},
    {GGML_TYPE_Q5_0, ggml_vec_dot_q5_0_q8_0_generic},
    {GGML_TYPE_Q5_1, ggml_vec_dot_q5_1_q8_1_generic},
    {GGML_TYPE_Q8_0, ggml_vec_dot_q8_0_q8_0_generic},
    {GGML_TYPE_MXFP4, ggml_vec_dot_mxfp4_q8_0_generic},
    {GGML_TYPE_NVFP4, ggml_vec_dot_nvfp4_q8_0_generic},
    {GGML_TYPE_Q2_K, ggml_vec_dot_q2_K_q8_K_generic},
    {GGML_TYPE_Q3_K, ggml_vec_dot_q3_K_q8_K_generic},
    {GGML_TYPE_Q4_K, ggml_vec_dot_q4_K_q8_K_generic},
    {GGML_TYPE_Q5_K, ggml_vec_dot_q5_K_q8_K_generic},
    {GGML_TYPE_Q6_K, ggml_vec_dot_q6_K_q8_K_generic},
    {GGML_TYPE_TQ1_0, ggml_vec_dot_tq1_0_q8_K_generic},
    {GGML_TYPE_TQ2_0, ggml_vec_dot_tq2_0_q8_K_generic},
    {GGML_TYPE_IQ2_XXS, ggml_vec_dot_iq2_xxs_q8_K_generic},
    {GGML_TYPE_IQ2_XS, ggml_vec_dot_iq2_xs_q8_K_generic},
    {GGML_TYPE_IQ2_S, ggml_vec_dot_iq2_s_q8_K_generic},
    {GGML_TYPE_IQ3_XXS, ggml_vec_dot_iq3_xxs_q8_K_generic},
    {GGML_TYPE_IQ3_S, ggml_vec_dot_iq3_s_q8_K_generic},
    {GGML_TYPE_IQ1_S, ggml_vec_dot_iq1_s_q8_K_generic},
    {GGML_TYPE_IQ1_M, ggml_vec_dot_iq1_m_q8_K_generic},
    {GGML_TYPE_IQ4_NL, ggml_vec_dot_iq4_nl_q8_0_generic},
    {GGML_TYPE_IQ4_XS, ggml_vec_dot_iq4_xs_q8_K_generic},
};

// The 9 repack combinations enumerated in
// ggml_repack_get_optimal_repack_type() (src/ggml-cpu/repack.cpp:4528-4560).
struct RepackEntry {
    RepackKey key;
    quantize_fn_t quantize_mat;
    quantize_fn_t quantize_mat_generic;
    gemx_fn_t gemv;
    gemx_fn_t gemv_generic;
    gemx_fn_t gemm;
    gemx_fn_t gemm_generic;
};

const RepackEntry k_repack_entries[] = {
    {{GGML_TYPE_Q4_0, GGML_TYPE_Q8_0, 4, 4, "q4_0_4x4_q8_0"},
     ggml_quantize_mat_q8_0_4x4,
     ggml_quantize_mat_q8_0_4x4_generic,
     ggml_gemv_q4_0_4x4_q8_0,
     ggml_gemv_q4_0_4x4_q8_0_generic,
     ggml_gemm_q4_0_4x4_q8_0,
     ggml_gemm_q4_0_4x4_q8_0_generic},
    {{GGML_TYPE_Q4_0, GGML_TYPE_Q8_0, 8, 4, "q4_0_4x8_q8_0"},
     ggml_quantize_mat_q8_0_4x8,
     ggml_quantize_mat_q8_0_4x8_generic,
     ggml_gemv_q4_0_4x8_q8_0,
     ggml_gemv_q4_0_4x8_q8_0_generic,
     ggml_gemm_q4_0_4x8_q8_0,
     ggml_gemm_q4_0_4x8_q8_0_generic},
    {{GGML_TYPE_Q4_0, GGML_TYPE_Q8_0, 8, 8, "q4_0_8x8_q8_0"},
     ggml_quantize_mat_q8_0_4x8,
     ggml_quantize_mat_q8_0_4x8_generic,
     ggml_gemv_q4_0_8x8_q8_0,
     ggml_gemv_q4_0_8x8_q8_0_generic,
     ggml_gemm_q4_0_8x8_q8_0,
     ggml_gemm_q4_0_8x8_q8_0_generic},
    {{GGML_TYPE_Q4_K, GGML_TYPE_Q8_K, 8, 4, "q4_K_8x4_q8_K"},
     ggml_quantize_mat_q8_K_4x4,
     ggml_quantize_mat_q8_K_4x4_generic,
     ggml_gemv_q4_K_8x4_q8_K,
     ggml_gemv_q4_K_8x4_q8_K_generic,
     ggml_gemm_q4_K_8x4_q8_K,
     ggml_gemm_q4_K_8x4_q8_K_generic},
    {{GGML_TYPE_Q4_K, GGML_TYPE_Q8_K, 8, 8, "q4_K_8x8_q8_K"},
     ggml_quantize_mat_q8_K_4x8,
     ggml_quantize_mat_q8_K_4x8_generic,
     ggml_gemv_q4_K_8x8_q8_K,
     ggml_gemv_q4_K_8x8_q8_K_generic,
     ggml_gemm_q4_K_8x8_q8_K,
     ggml_gemm_q4_K_8x8_q8_K_generic},
    {{GGML_TYPE_Q5_K, GGML_TYPE_Q8_K, 8, 4, "q5_K_8x4_q8_K"},
     ggml_quantize_mat_q8_K_4x4,
     ggml_quantize_mat_q8_K_4x4_generic,
     ggml_gemv_q5_K_8x4_q8_K,
     ggml_gemv_q5_K_8x4_q8_K_generic,
     ggml_gemm_q5_K_8x4_q8_K,
     ggml_gemm_q5_K_8x4_q8_K_generic},
    {{GGML_TYPE_Q5_K, GGML_TYPE_Q8_K, 8, 8, "q5_K_8x8_q8_K"},
     ggml_quantize_mat_q8_K_4x8,
     ggml_quantize_mat_q8_K_4x8_generic,
     ggml_gemv_q5_K_8x8_q8_K,
     ggml_gemv_q5_K_8x8_q8_K_generic,
     ggml_gemm_q5_K_8x8_q8_K,
     ggml_gemm_q5_K_8x8_q8_K_generic},
    {{GGML_TYPE_Q6_K, GGML_TYPE_Q8_K, 8, 4, "q6_K_8x4_q8_K"},
     ggml_quantize_mat_q8_K_4x4,
     ggml_quantize_mat_q8_K_4x4_generic,
     ggml_gemv_q6_K_8x4_q8_K,
     ggml_gemv_q6_K_8x4_q8_K_generic,
     ggml_gemm_q6_K_8x4_q8_K,
     ggml_gemm_q6_K_8x4_q8_K_generic},
    {{GGML_TYPE_Q6_K, GGML_TYPE_Q8_K, 8, 8, "q6_K_8x8_q8_K"},
     ggml_quantize_mat_q8_K_4x8,
     ggml_quantize_mat_q8_K_4x8_generic,
     ggml_gemv_q6_K_8x8_q8_K,
     ggml_gemv_q6_K_8x8_q8_K_generic,
     ggml_gemm_q6_K_8x8_q8_K,
     ggml_gemm_q6_K_8x8_q8_K_generic},
    {{GGML_TYPE_Q2_K, GGML_TYPE_Q8_K, 8, 8, "q2_K_8x8_q8_K"},
     ggml_quantize_mat_q8_K_4x8,
     ggml_quantize_mat_q8_K_4x8_generic,
     ggml_gemv_q2_K_8x8_q8_K,
     ggml_gemv_q2_K_8x8_q8_K_generic,
     ggml_gemm_q2_K_8x8_q8_K,
     ggml_gemm_q2_K_8x8_q8_K_generic},
    {{GGML_TYPE_IQ4_NL, GGML_TYPE_Q8_0, 4, 4, "iq4_nl_4x4_q8_0"},
     ggml_quantize_mat_q8_0_4x4,
     ggml_quantize_mat_q8_0_4x4_generic,
     ggml_gemv_iq4_nl_4x4_q8_0,
     ggml_gemv_iq4_nl_4x4_q8_0_generic,
     ggml_gemm_iq4_nl_4x4_q8_0,
     ggml_gemm_iq4_nl_4x4_q8_0_generic},
    {{GGML_TYPE_IQ4_NL, GGML_TYPE_Q8_0, 8, 8, "iq4_nl_8x8_q8_0"},
     ggml_quantize_mat_q8_0_4x8,
     ggml_quantize_mat_q8_0_4x8_generic,
     ggml_gemv_iq4_nl_8x8_q8_0,
     ggml_gemv_iq4_nl_8x8_q8_0_generic,
     ggml_gemm_iq4_nl_8x8_q8_0,
     ggml_gemm_iq4_nl_8x8_q8_0_generic},
    {{GGML_TYPE_MXFP4, GGML_TYPE_Q8_0, 4, 4, "mxfp4_4x4_q8_0"},
     ggml_quantize_mat_q8_0_4x4,
     ggml_quantize_mat_q8_0_4x4_generic,
     ggml_gemv_mxfp4_4x4_q8_0,
     ggml_gemv_mxfp4_4x4_q8_0_generic,
     ggml_gemm_mxfp4_4x4_q8_0,
     ggml_gemm_mxfp4_4x4_q8_0_generic},
    {{GGML_TYPE_MXFP4, GGML_TYPE_Q8_0, 8, 8, "mxfp4_8x8_q8_0"},
     ggml_quantize_mat_q8_0_4x8,
     ggml_quantize_mat_q8_0_4x8_generic,
     ggml_gemv_mxfp4_8x8_q8_0,
     ggml_gemv_mxfp4_8x8_q8_0_generic,
     ggml_gemm_mxfp4_8x8_q8_0,
     ggml_gemm_mxfp4_8x8_q8_0_generic},
    {{GGML_TYPE_Q8_0, GGML_TYPE_Q8_0, 4, 4, "q8_0_4x4_q8_0"},
     ggml_quantize_mat_q8_0_4x4,
     ggml_quantize_mat_q8_0_4x4_generic,
     ggml_gemv_q8_0_4x4_q8_0,
     ggml_gemv_q8_0_4x4_q8_0_generic,
     ggml_gemm_q8_0_4x4_q8_0,
     ggml_gemm_q8_0_4x4_q8_0_generic},
    {{GGML_TYPE_Q8_0, GGML_TYPE_Q8_0, 8, 4, "q8_0_4x8_q8_0"},
     ggml_quantize_mat_q8_0_4x8,
     ggml_quantize_mat_q8_0_4x8_generic,
     ggml_gemv_q8_0_4x8_q8_0,
     ggml_gemv_q8_0_4x8_q8_0_generic,
     ggml_gemm_q8_0_4x8_q8_0,
     ggml_gemm_q8_0_4x8_q8_0_generic},
};

// Thin adapters from GGML's whole-matrix `(src, dst, nrows, n_per_row,
// imatrix)` quantizer signature (see ggml_internal_abi.h) down to
// quantize_fn_t's flat `(x, y, k)` shape, matching what every other type's
// from_float_ref already looks like: one row, no importance weighting. All
// four of these implementations actually require a non-null quant_weights
// pointer (a real GGML_ASSERT for IQ2_XXS/IQ2_XS/IQ1_S; commented out but
// still exercised for IQ1_M) -- a uniform (all-1.0) weighting is passed so
// every element is treated as equally important, the closest equivalent to
// "no importance weighting" these quantizers support.
void quantize_iq2_xxs_row(const float *x, void *y, int64_t k) {
    std::vector<float> w(k, 1.0f);
    quantize_iq2_xxs(x, y, 1, k, w.data());
}
void quantize_iq2_xs_row(const float *x, void *y, int64_t k) {
    std::vector<float> w(k, 1.0f);
    quantize_iq2_xs(x, y, 1, k, w.data());
}
void quantize_iq1_s_row(const float *x, void *y, int64_t k) {
    std::vector<float> w(k, 1.0f);
    quantize_iq1_s(x, y, 1, k, w.data());
}
void quantize_iq1_m_row(const float *x, void *y, int64_t k) {
    std::vector<float> w(k, 1.0f);
    quantize_iq1_m(x, y, 1, k, w.data());
}

}  // namespace

void register_ggml_provider(KernelRegistries &registries) {
    ggml_cpu_init();

    // -- quantize / dequantize: fully public API --
    for (int t = 0; t < GGML_TYPE_COUNT; ++t) {
        const ggml_type type = static_cast<ggml_type>(t);
        const ggml_type_traits *tt = ggml_get_type_traits(type);
        const ggml_type_traits_cpu *tc = ggml_get_type_traits_cpu(type);
        if (!tt || !tc) {
            continue;
        }
        // Deliberately NOT calling ggml_quantize_init(type) here: for
        // IQ2_XXS/IQ2_XS/IQ2_S/IQ1_S/IQ1_M/IQ3_XXS/IQ3_S it builds a nearest-
        // neighbor lookup table via an O(43692 * grid_size) search with a
        // qsort per row (src/ggml-quants.c, iq2xs_init_impl/iq3xs_init_impl)
        // -- genuinely slow (hundreds of ms), and doing it here for all 42
        // types unconditionally at registration time means paying for it
        // before any benchmark has printed a single row, even for runs
        // (e.g. --repack) that never touch these types at all. Each bench_*.cpp
        // calls it lazily, once, right before it first actually invokes one
        // of these types' quantize/dequantize/vec_dot functions instead.

        if (tt->from_float_ref) {
            registries.quantize.register_reference(type, "ggml-ref", tt->from_float_ref);
            if (tc->from_float) {
                registries.quantize.register_candidate(type, "ggml-cpu", tc->from_float);
            }
        }
        if (tt->to_float) {
            registries.dequantize.register_reference(type, "ggml-ref", tt->to_float);
            // No candidate registered yet: GGML has exactly one dequantize
            // implementation per type (src/ggml-quants.c, arch-independent).
            // This is where a from-scratch dequantizer plugs in later.
        }
    }

    // GGML_TYPE_Q8_K has no public from_float_ref (see quantize_row_q8_K_generic's
    // doc comment in ggml_internal_abi.h) -- it's the activation format for every
    // K-quant vec_dot/gemv/gemm, so without this, all of those silently have no
    // valid input to quantize into and get skipped by the benchmarks.
    {
        const ggml_type_traits_cpu *tc = ggml_get_type_traits_cpu(GGML_TYPE_Q8_K);
        registries.quantize.register_reference(GGML_TYPE_Q8_K, "ggml-generic", quantize_row_q8_K_generic);
        if (tc && tc->from_float) {
            registries.quantize.register_candidate(GGML_TYPE_Q8_K, "ggml-cpu", tc->from_float);
        }
    }

    // GGML_TYPE_IQ2_XXS/IQ2_XS/IQ1_S/IQ1_M have no public from_float_ref
    // either (see quantize_iq2_xxs's doc comment in ggml_internal_abi.h) --
    // they're importance-matrix-only codebook types whose only public
    // quantizer takes a different, whole-matrix signature. Without this,
    // these 4 types would never appear in the quantize/dequantize
    // benchmarks at all (bench_dequantize.cpp requires both a quantize and
    // a dequantize reference to exist before it will test a type).
    registries.quantize.register_reference(GGML_TYPE_IQ2_XXS, "ggml-ref", quantize_iq2_xxs_row);
    registries.quantize.register_reference(GGML_TYPE_IQ2_XS, "ggml-ref", quantize_iq2_xs_row);
    registries.quantize.register_reference(GGML_TYPE_IQ1_S, "ggml-ref", quantize_iq1_s_row);
    registries.quantize.register_reference(GGML_TYPE_IQ1_M, "ggml-ref", quantize_iq1_m_row);

    // arch-fallback.h #defines a `_generic` symbol onto its canonical
    // counterpart -- inside GGML's own source files -- for whichever
    // functions the current architecture has no distinct optimized version
    // of, and which functions that applies to varies by architecture. The
    // `_generic` declarations in ggml_internal_abi.h are marked
    // GGML_BENCH_WEAK precisely so that case resolves to a null function
    // pointer here instead of a link error: when null, there is only one
    // real implementation, so it becomes the reference with no candidate,
    // rather than fabricating a comparison against nothing.
    auto register_pair = [](auto &registry, const auto &key, auto generic_fn, auto canonical_fn) {
        if (generic_fn) {
            registry.register_reference(key, "ggml-generic", generic_fn);
            if (canonical_fn) {
                registry.register_candidate(key, "ggml-cpu", canonical_fn);
            }
        } else if (canonical_fn) {
            registry.register_reference(key, "ggml-cpu", canonical_fn);
        }
    };

    // -- vec_dot: public candidate, private (possibly weak-null) reference --
    for (const auto &ref : k_vec_dot_refs) {
        const ggml_type_traits_cpu *tc = ggml_get_type_traits_cpu(ref.type);
        register_pair(registries.vec_dot, ref.type, ref.fn, tc ? tc->vec_dot : nullptr);
    }

    // -- repack: private reference and candidate (no public accessor exists) --
    for (const auto &e : k_repack_entries) {
        register_pair(registries.repack_quantize_mat, e.key, e.quantize_mat_generic, e.quantize_mat);
        register_pair(registries.repack_gemv, e.key, e.gemv_generic, e.gemv);
        register_pair(registries.repack_gemm, e.key, e.gemm_generic, e.gemm);
    }
}
