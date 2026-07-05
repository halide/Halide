#include "halide_provider.h"

#include <ggml_quants.h>

void register_halide_provider(KernelRegistries &registries) {
    registries.quantize.register_candidate(GGML_TYPE_Q4_0, "halide", ggml_quants_halide_quantize_q4_0);
    registries.dequantize.register_candidate(GGML_TYPE_Q4_0, "halide", ggml_quants_halide_dequantize_q4_0);
    registries.vec_dot.register_candidate(GGML_TYPE_Q4_0, "halide", ggml_quants_halide_vec_dot_q4_0_q8_0);

    registries.quantize.register_candidate(GGML_TYPE_Q4_1, "halide", ggml_quants_halide_quantize_q4_1);
    registries.dequantize.register_candidate(GGML_TYPE_Q4_1, "halide", ggml_quants_halide_dequantize_q4_1);
    registries.vec_dot.register_candidate(GGML_TYPE_Q4_1, "halide", ggml_quants_halide_vec_dot_q4_1_q8_1);

    registries.quantize.register_candidate(GGML_TYPE_Q5_0, "halide", ggml_quants_halide_quantize_q5_0);
    registries.dequantize.register_candidate(GGML_TYPE_Q5_0, "halide", ggml_quants_halide_dequantize_q5_0);
    registries.vec_dot.register_candidate(GGML_TYPE_Q5_0, "halide", ggml_quants_halide_vec_dot_q5_0_q8_0);

    registries.quantize.register_candidate(GGML_TYPE_Q5_1, "halide", ggml_quants_halide_quantize_q5_1);
    registries.dequantize.register_candidate(GGML_TYPE_Q5_1, "halide", ggml_quants_halide_dequantize_q5_1);
    registries.vec_dot.register_candidate(GGML_TYPE_Q5_1, "halide", ggml_quants_halide_vec_dot_q5_1_q8_1);

    registries.quantize.register_candidate(GGML_TYPE_Q8_0, "halide", ggml_quants_halide_quantize_q8_0);
    registries.dequantize.register_candidate(GGML_TYPE_Q8_0, "halide", ggml_quants_halide_dequantize_q8_0);
    registries.vec_dot.register_candidate(GGML_TYPE_Q8_0, "halide", ggml_quants_halide_vec_dot_q8_0_q8_0);

    // Q8_1 is activation-only (GGML has no public to_float for it, so
    // ggml_provider.cpp registers no dequantize reference either -- the
    // harness's bench_dequantize.cpp already skips types with no reference).
    registries.quantize.register_candidate(GGML_TYPE_Q8_1, "halide", ggml_quants_halide_quantize_q8_1);

    // Q8_K is also activation-only, but unlike Q8_1, GGML doesn't even
    // register a public from_float_ref for it -- ggml_provider.cpp's
    // special case (using the private quantize_row_q8_K_generic) is what
    // supplies the reference this candidate is compared against.
    registries.quantize.register_candidate(GGML_TYPE_Q8_K, "halide", ggml_quants_halide_quantize_q8_k);

    // Q2_K, Q6_K: dequantize is a genuine from-scratch Halide candidate.
    // Quantize is scaffolding that itself calls out to GGML's own
    // reference (see halide/ggml_extern_quantize.cpp) pending a
    // from-scratch port of GGML's iterative scale search -- it's still
    // registered as a candidate so the harness's plumbing is exercised
    // end-to-end, but it will trivially match (same underlying code path).
    // vec_dot is a genuine from-scratch candidate (against Q8_K).
    registries.quantize.register_candidate(GGML_TYPE_Q2_K, "halide", ggml_quants_halide_quantize_q2_k);
    registries.dequantize.register_candidate(GGML_TYPE_Q2_K, "halide", ggml_quants_halide_dequantize_q2_k);
    registries.vec_dot.register_candidate(GGML_TYPE_Q2_K, "halide", ggml_quants_halide_vec_dot_q2_k_q8_k);

    registries.quantize.register_candidate(GGML_TYPE_Q6_K, "halide", ggml_quants_halide_quantize_q6_k);
    registries.dequantize.register_candidate(GGML_TYPE_Q6_K, "halide", ggml_quants_halide_dequantize_q6_k);
    registries.vec_dot.register_candidate(GGML_TYPE_Q6_K, "halide", ggml_quants_halide_vec_dot_q6_k_q8_k);

    registries.quantize.register_candidate(GGML_TYPE_Q4_K, "halide", ggml_quants_halide_quantize_q4_k);
    registries.dequantize.register_candidate(GGML_TYPE_Q4_K, "halide", ggml_quants_halide_dequantize_q4_k);
    registries.vec_dot.register_candidate(GGML_TYPE_Q4_K, "halide", ggml_quants_halide_vec_dot_q4_k_q8_k);

    registries.quantize.register_candidate(GGML_TYPE_Q5_K, "halide", ggml_quants_halide_quantize_q5_k);
    registries.dequantize.register_candidate(GGML_TYPE_Q5_K, "halide", ggml_quants_halide_dequantize_q5_k);
    registries.vec_dot.register_candidate(GGML_TYPE_Q5_K, "halide", ggml_quants_halide_vec_dot_q5_k_q8_k);

    registries.quantize.register_candidate(GGML_TYPE_Q3_K, "halide", ggml_quants_halide_quantize_q3_k);
    registries.dequantize.register_candidate(GGML_TYPE_Q3_K, "halide", ggml_quants_halide_dequantize_q3_k);
    registries.vec_dot.register_candidate(GGML_TYPE_Q3_K, "halide", ggml_quants_halide_vec_dot_q3_k_q8_k);

    registries.quantize.register_candidate(GGML_TYPE_Q1_0, "halide", ggml_quants_halide_quantize_q1_0);
    registries.dequantize.register_candidate(GGML_TYPE_Q1_0, "halide", ggml_quants_halide_dequantize_q1_0);
    registries.vec_dot.register_candidate(GGML_TYPE_Q1_0, "halide", ggml_quants_halide_vec_dot_q1_0_q8_0);

    registries.quantize.register_candidate(GGML_TYPE_MXFP4, "halide", ggml_quants_halide_quantize_mxfp4);
    registries.dequantize.register_candidate(GGML_TYPE_MXFP4, "halide", ggml_quants_halide_dequantize_mxfp4);
    registries.vec_dot.register_candidate(GGML_TYPE_MXFP4, "halide", ggml_quants_halide_vec_dot_mxfp4_q8_0);

    registries.quantize.register_candidate(GGML_TYPE_NVFP4, "halide", ggml_quants_halide_quantize_nvfp4);
    registries.dequantize.register_candidate(GGML_TYPE_NVFP4, "halide", ggml_quants_halide_dequantize_nvfp4);
    registries.vec_dot.register_candidate(GGML_TYPE_NVFP4, "halide", ggml_quants_halide_vec_dot_nvfp4_q8_0);

    registries.quantize.register_candidate(GGML_TYPE_IQ4_NL, "halide", ggml_quants_halide_quantize_iq4_nl);
    registries.dequantize.register_candidate(GGML_TYPE_IQ4_NL, "halide", ggml_quants_halide_dequantize_iq4_nl);
    registries.vec_dot.register_candidate(GGML_TYPE_IQ4_NL, "halide", ggml_quants_halide_vec_dot_iq4_nl_q8_0);

    registries.quantize.register_candidate(GGML_TYPE_IQ4_XS, "halide", ggml_quants_halide_quantize_iq4_xs);
    registries.dequantize.register_candidate(GGML_TYPE_IQ4_XS, "halide", ggml_quants_halide_dequantize_iq4_xs);
    registries.vec_dot.register_candidate(GGML_TYPE_IQ4_XS, "halide", ggml_quants_halide_vec_dot_iq4_xs_q8_k);

    registries.quantize.register_candidate(GGML_TYPE_TQ1_0, "halide", ggml_quants_halide_quantize_tq1_0);
    registries.dequantize.register_candidate(GGML_TYPE_TQ1_0, "halide", ggml_quants_halide_dequantize_tq1_0);
    registries.vec_dot.register_candidate(GGML_TYPE_TQ1_0, "halide", ggml_quants_halide_vec_dot_tq1_0_q8_k);

    registries.quantize.register_candidate(GGML_TYPE_TQ2_0, "halide", ggml_quants_halide_quantize_tq2_0);
    registries.dequantize.register_candidate(GGML_TYPE_TQ2_0, "halide", ggml_quants_halide_dequantize_tq2_0);
    registries.vec_dot.register_candidate(GGML_TYPE_TQ2_0, "halide", ggml_quants_halide_vec_dot_tq2_0_q8_k);

    // IQ2_XXS: dequantize only (see ggml_quants.h for why); vec_dot is still
    // a genuine from-scratch candidate (GGML's own reference quantizer is
    // used to produce the test/benchmark input, via ggml_provider.cpp).
    registries.dequantize.register_candidate(GGML_TYPE_IQ2_XXS, "halide", ggml_quants_halide_dequantize_iq2_xxs);
    registries.vec_dot.register_candidate(GGML_TYPE_IQ2_XXS, "halide", ggml_quants_halide_vec_dot_iq2_xxs_q8_k);

    registries.dequantize.register_candidate(GGML_TYPE_IQ2_XS, "halide", ggml_quants_halide_dequantize_iq2_xs);
    registries.vec_dot.register_candidate(GGML_TYPE_IQ2_XS, "halide", ggml_quants_halide_vec_dot_iq2_xs_q8_k);

    registries.quantize.register_candidate(GGML_TYPE_IQ2_S, "halide", ggml_quants_halide_quantize_iq2_s);
    registries.dequantize.register_candidate(GGML_TYPE_IQ2_S, "halide", ggml_quants_halide_dequantize_iq2_s);
    registries.vec_dot.register_candidate(GGML_TYPE_IQ2_S, "halide", ggml_quants_halide_vec_dot_iq2_s_q8_k);

    registries.quantize.register_candidate(GGML_TYPE_IQ3_XXS, "halide", ggml_quants_halide_quantize_iq3_xxs);
    registries.dequantize.register_candidate(GGML_TYPE_IQ3_XXS, "halide", ggml_quants_halide_dequantize_iq3_xxs);
    registries.vec_dot.register_candidate(GGML_TYPE_IQ3_XXS, "halide", ggml_quants_halide_vec_dot_iq3_xxs_q8_k);

    registries.quantize.register_candidate(GGML_TYPE_IQ3_S, "halide", ggml_quants_halide_quantize_iq3_s);
    registries.dequantize.register_candidate(GGML_TYPE_IQ3_S, "halide", ggml_quants_halide_dequantize_iq3_s);
    registries.vec_dot.register_candidate(GGML_TYPE_IQ3_S, "halide", ggml_quants_halide_vec_dot_iq3_s_q8_k);

    registries.dequantize.register_candidate(GGML_TYPE_IQ1_S, "halide", ggml_quants_halide_dequantize_iq1_s);
    registries.vec_dot.register_candidate(GGML_TYPE_IQ1_S, "halide", ggml_quants_halide_vec_dot_iq1_s_q8_k);

    registries.dequantize.register_candidate(GGML_TYPE_IQ1_M, "halide", ggml_quants_halide_dequantize_iq1_m);
    registries.vec_dot.register_candidate(GGML_TYPE_IQ1_M, "halide", ggml_quants_halide_vec_dot_iq1_m_q8_k);

    // F16, BF16: plain float casts, not "quantized" types, but both
    // directions are fully native Halide. No vec_dot: not part of the
    // quantized-format vec_dot sweep this directory otherwise covers.
    registries.quantize.register_candidate(GGML_TYPE_F16, "halide", ggml_quants_halide_quantize_f16);
    registries.dequantize.register_candidate(GGML_TYPE_F16, "halide", ggml_quants_halide_dequantize_f16);

    registries.quantize.register_candidate(GGML_TYPE_BF16, "halide", ggml_quants_halide_quantize_bf16);
    registries.dequantize.register_candidate(GGML_TYPE_BF16, "halide", ggml_quants_halide_dequantize_bf16);
}
