add_halide_generator(
    quants.generator
    SOURCES
    f16_generators.cpp
    bf16_generators.cpp
    repack_quantize_mat_generators.cpp
    repack_matmul_generator.cpp
    symmetric_quant_generators.cpp
    symmetric_vec_dot_generator.cpp
    lookup_table_quant_generators.cpp
    lookup_table_vec_dot_generator.cpp
    k_quant_vec_dot_generator.cpp
    k_quant_generators.cpp
)

# Q4_0's and Q8_0's quantize/dequantize/vec_dot kernels are GENERATOR_ARGS
# instantiations of the generic, reusable Approximation-based
# symmetric_quantize/symmetric_dequantize/symmetric_vec_dot generators (see
# symmetric_quant_generators.cpp/symmetric_vec_dot_generator.cpp and
# quant_components.h) -- not their own per-format C++ Generator classes. See
# quant_components.h's RoundingMode/ScaleAnchor for what these params mean.
add_halide_library(
    q4_0_quantize
    FROM quants.generator
    GENERATOR symmetric_quantize
    PARAMS
    block_size=32 qmax=8 code_bits=4 rounding=truncate_half_up_with_offset anchor=extreme_signed
)
add_halide_library(
    q4_0_dequantize
    FROM quants.generator
    GENERATOR symmetric_dequantize
    PARAMS
    block_size=32 qmax=8 code_bits=4 rounding=truncate_half_up_with_offset anchor=extreme_signed
)
add_halide_library(
    q4_0_vec_dot
    FROM quants.generator
    GENERATOR symmetric_vec_dot
    PARAMS
    w_kind=symmetric block_size=32 w_qmax=8 w_code_bits=4 w_rounding=truncate_half_up_with_offset
    w_anchor=extreme_signed a_kind=q8_0 a_qmax=127
)
# Q4_1 is affine (min+scale, not symmetric): quant_components.h's
# AffineQuantize + NibblePack, matching block_q4_1's {fp16 d; fp16 m; qs[16];}.
add_halide_library(
    q4_1_quantize
    FROM quants.generator
    GENERATOR symmetric_quantize
    PARAMS kind=affine block_size=32 levels=15 code_bits=4 affine_rounding=clamped_int8
)
add_halide_library(
    q4_1_dequantize
    FROM quants.generator
    GENERATOR symmetric_dequantize
    PARAMS kind=affine block_size=32 levels=15 code_bits=4 affine_rounding=clamped_int8
)
add_halide_library(
    q4_1_vec_dot
    FROM quants.generator
    GENERATOR symmetric_vec_dot
    PARAMS
    w_kind=affine block_size=32 w_levels=15 w_code_bits=4 w_affine_rounding=clamped_int8 a_kind=q8_1
    a_qmax=127
)
# Q5_0 is symmetric like Q4_0, but 5-bit: quant_components.h's
# SymmetricAffineQuantize + FiveBitPack, matching block_q5_0's
# {fp16 d; qh[4]; qs[16];}.
add_halide_library(
    q5_0_quantize
    FROM quants.generator
    GENERATOR symmetric_quantize
    PARAMS kind=symmetric_5bit block_size=32 qmax=16
)
add_halide_library(
    q5_0_dequantize
    FROM quants.generator
    GENERATOR symmetric_dequantize
    PARAMS kind=symmetric_5bit block_size=32 qmax=16
)
add_halide_library(
    q5_0_vec_dot
    FROM quants.generator
    GENERATOR symmetric_vec_dot
    PARAMS w_kind=symmetric_5bit block_size=32 w_qmax=16 a_kind=q8_0 a_qmax=127
)
# Q5_1 is affine like Q4_1, but 5-bit: quant_components.h's AffineQuantize +
# FiveBitPack, matching block_q5_1's {fp16 d; fp16 m; qh[4]; qs[16];}.
add_halide_library(
    q5_1_quantize
    FROM quants.generator
    GENERATOR symmetric_quantize
    PARAMS kind=affine_5bit block_size=32 levels=31 affine_rounding=unclamped_uint8
)
add_halide_library(
    q5_1_dequantize
    FROM quants.generator
    GENERATOR symmetric_dequantize
    PARAMS kind=affine_5bit block_size=32 levels=31 affine_rounding=unclamped_uint8
)
add_halide_library(
    q5_1_vec_dot
    FROM quants.generator
    GENERATOR symmetric_vec_dot
    PARAMS
    w_kind=affine_5bit block_size=32 w_levels=31 w_affine_rounding=unclamped_uint8 a_kind=q8_1
    a_qmax=127
)
add_halide_library(
    q8_0_quantize
    FROM quants.generator
    GENERATOR symmetric_quantize
    PARAMS block_size=32 qmax=127 code_bits=8 rounding=nearest anchor=abs_max
)
add_halide_library(
    q8_0_dequantize
    FROM quants.generator
    GENERATOR symmetric_dequantize
    PARAMS block_size=32 qmax=127 code_bits=8 rounding=nearest anchor=abs_max
)
add_halide_library(
    q8_0_vec_dot
    FROM quants.generator
    GENERATOR symmetric_vec_dot
    PARAMS
    w_kind=symmetric block_size=32 w_qmax=127 w_code_bits=8 w_rounding=nearest w_anchor=abs_max
    a_kind=q8_0 a_qmax=127
)
# Q8_1 is symmetric byte-packed like Q8_0, plus AppendCodeSum's derived 's'
# field, matching block_q8_1's {fp16 d; fp16 s; qs[32];}. Activation-only
# (no public to_float, see q8_1_generators.cpp's header comment -- it's
# gone now, but this scheme's decode() is still used by any vec_dot pairing
# against Q8_1), so there's no q8_1_dequantize library.
add_halide_library(
    q8_1_quantize
    FROM quants.generator
    GENERATOR symmetric_quantize
    PARAMS kind=symmetric_byte_sum block_size=32 qmax=127
)
# Q8_K's quantize kernel (activation-only, no dequantize -- see
# q8_k_generators.cpp) is a GENERATOR_ARGS instantiation of the generic,
# reusable Approximation-based symmetric_quantize generator (see
# symmetric_quant_generators.cpp and quant_components.h's
# AppendGroupSumsInt16/F32Pack/RoundingMode::NearestEvenClampedHigh/
# ScaleAnchor::ExtremeSignedValueTwoStep).
add_halide_library(
    q8_k_quantize
    FROM quants.generator
    GENERATOR symmetric_quantize
    PARAMS kind=q8k block_size=256 qmax=127
)
# Q2_K's quantize/dequantize kernels are GENERATOR_ARGS instantiations of
# the generic, reusable Approximation-based k_quant_quantize/
# k_quant_dequantize generators (see k_quant_generators.cpp and
# quant_components.h's KQuantDequantize/NibblePairPack/PlanarBitPack).
add_halide_library(
    q2_k_quantize
    FROM quants.generator
    GENERATOR k_quant_quantize
    PARAMS family=q2_k
)
add_halide_library(
    q2_k_dequantize
    FROM quants.generator
    GENERATOR k_quant_dequantize
    PARAMS family=q2_k
)
add_halide_library(q2_k_vec_dot FROM quants.generator GENERATOR k_quant_vec_dot PARAMS family=q2_k)
# Q6_K's quantize/dequantize kernels are GENERATOR_ARGS instantiations of
# the generic, reusable Approximation-based k_quant_quantize/
# k_quant_dequantize generators (see k_quant_generators.cpp and
# quant_components.h's KQuantDequantize/CombinedBitsCode/BytePack).
add_halide_library(
    q6_k_quantize
    FROM quants.generator
    GENERATOR k_quant_quantize
    PARAMS family=q6_k
)
add_halide_library(
    q6_k_dequantize
    FROM quants.generator
    GENERATOR k_quant_dequantize
    PARAMS family=q6_k
)
add_halide_library(q6_k_vec_dot FROM quants.generator GENERATOR k_quant_vec_dot PARAMS family=q6_k)
# Q4_K's quantize/dequantize kernels are GENERATOR_ARGS instantiations of
# the generic, reusable Approximation-based k_quant_quantize/
# k_quant_dequantize generators (see k_quant_generators.cpp and
# quant_components.h's KQuantDequantize/K4ScaleMinPack/PlanarBitPack).
add_halide_library(
    q4_k_quantize
    FROM quants.generator
    GENERATOR k_quant_quantize
    PARAMS family=q4_k
)
add_halide_library(
    q4_k_dequantize
    FROM quants.generator
    GENERATOR k_quant_dequantize
    PARAMS family=q4_k
)
add_halide_library(q4_k_vec_dot FROM quants.generator GENERATOR k_quant_vec_dot PARAMS family=q4_k)
# Q5_K's quantize/dequantize kernels are GENERATOR_ARGS instantiations of
# the generic, reusable Approximation-based k_quant_quantize/
# k_quant_dequantize generators (see k_quant_generators.cpp and
# quant_components.h's KQuantDequantize/K4ScaleMinPack/CombinedBitsCode).
add_halide_library(
    q5_k_quantize
    FROM quants.generator
    GENERATOR k_quant_quantize
    PARAMS family=q5_k
)
add_halide_library(
    q5_k_dequantize
    FROM quants.generator
    GENERATOR k_quant_dequantize
    PARAMS family=q5_k
)
add_halide_library(q5_k_vec_dot FROM quants.generator GENERATOR k_quant_vec_dot PARAMS family=q5_k)
# Q3_K's quantize/dequantize kernels are GENERATOR_ARGS instantiations of
# the generic, reusable Approximation-based k_quant_quantize/
# k_quant_dequantize generators (see k_quant_generators.cpp and
# quant_components.h's KQuantDequantize/Q3KScalePack/CombinedBitsCode).
add_halide_library(
    q3_k_quantize
    FROM quants.generator
    GENERATOR k_quant_quantize
    PARAMS family=q3_k
)
add_halide_library(
    q3_k_dequantize
    FROM quants.generator
    GENERATOR k_quant_dequantize
    PARAMS family=q3_k
)
add_halide_library(q3_k_vec_dot FROM quants.generator GENERATOR k_quant_vec_dot PARAMS family=q3_k)
# Q1_0 is symmetric with a mean-abs scale and sign-only (1-bit) codes:
# quant_components.h's SymmetricAffineQuantize (ScaleAnchor::MeanAbs,
# RoundingMode::SignOnly) + BitPack, matching block_q1_0's {fp16 d; qs[16];}.
add_halide_library(
    q1_0_quantize
    FROM quants.generator
    GENERATOR symmetric_quantize
    PARAMS block_size=128 qmax=1 code_bits=1 rounding=sign_only anchor=mean_abs
)
add_halide_library(
    q1_0_dequantize
    FROM quants.generator
    GENERATOR symmetric_dequantize
    PARAMS block_size=128 qmax=1 code_bits=1 rounding=sign_only anchor=mean_abs
)
add_halide_library(
    q1_0_vec_dot
    FROM quants.generator
    GENERATOR symmetric_vec_dot
    PARAMS
    w_kind=symmetric block_size=128 w_qmax=1 w_code_bits=1 w_rounding=sign_only w_anchor=mean_abs
    a_kind=q8_0 a_qmax=127
)
# MXFP4/IQ4_NL's quantize/dequantize kernels are GENERATOR_ARGS
# instantiations of the generic, reusable Approximation-based
# lookup_table_quantize/lookup_table_dequantize generators (see
# lookup_table_quant_generators.cpp and quant_components.h's
# LookupTableQuantize/E8M0Pack) -- not their own per-format C++ Generator
# classes. vec_dot is still its own hand-rolled, unscheduled Generator (see
# mxfp4_generators.cpp/iq4_nl_generators.cpp), matching Q4_0/Q8_0's own
# treatment before they got the symmetric_vec_dot generic generator.
add_halide_library(
    mxfp4_quantize
    FROM quants.generator
    GENERATOR lookup_table_quantize
    PARAMS family=mxfp4
)
add_halide_library(
    mxfp4_dequantize
    FROM quants.generator
    GENERATOR lookup_table_dequantize
    PARAMS family=mxfp4
)
add_halide_library(
    mxfp4_vec_dot
    FROM quants.generator
    GENERATOR lookup_table_vec_dot
    PARAMS family=mxfp4
)
# NVFP4's quantize/dequantize kernels are GENERATOR_ARGS instantiations of
# the generic, reusable Approximation-based lookup_table_quantize/
# lookup_table_dequantize generators (see lookup_table_quant_generators.cpp
# and quant_components.h's LookupTableQuantize's num_scales/UE4M3Pack).
add_halide_library(
    nvfp4_quantize
    FROM quants.generator
    GENERATOR lookup_table_quantize
    PARAMS family=nvfp4
)
add_halide_library(
    nvfp4_dequantize
    FROM quants.generator
    GENERATOR lookup_table_dequantize
    PARAMS family=nvfp4
)
add_halide_library(
    nvfp4_vec_dot
    FROM quants.generator
    GENERATOR lookup_table_vec_dot
    PARAMS family=nvfp4
)
add_halide_library(
    iq4_nl_quantize
    FROM quants.generator
    GENERATOR lookup_table_quantize
    PARAMS family=iq4_nl
)
add_halide_library(
    iq4_nl_dequantize
    FROM quants.generator
    GENERATOR lookup_table_dequantize
    PARAMS family=iq4_nl
)
add_halide_library(
    iq4_nl_vec_dot
    FROM quants.generator
    GENERATOR lookup_table_vec_dot
    PARAMS family=iq4_nl
)
# IQ4_XS's quantize/dequantize kernels are GENERATOR_ARGS instantiations of
# the generic, reusable Approximation-based lookup_table_quantize/
# lookup_table_dequantize generators (see lookup_table_quant_generators.cpp
# and quant_components.h's IQ4XSDequantize).
add_halide_library(
    iq4_xs_quantize
    FROM quants.generator
    GENERATOR lookup_table_quantize
    PARAMS family=iq4_xs
)
add_halide_library(
    iq4_xs_dequantize
    FROM quants.generator
    GENERATOR lookup_table_dequantize
    PARAMS family=iq4_xs
)
add_halide_library(
    iq4_xs_vec_dot
    FROM quants.generator
    GENERATOR lookup_table_vec_dot
    PARAMS family=iq4_xs
)
# TQ1_0's quantize/dequantize kernels are GENERATOR_ARGS instantiations of
# the generic, reusable Approximation-based lookup_table_quantize/
# lookup_table_dequantize generators (see lookup_table_quant_generators.cpp
# and quant_components.h's LookupTableQuantize/TritPack).
add_halide_library(
    tq1_0_quantize
    FROM quants.generator
    GENERATOR lookup_table_quantize
    PARAMS family=tq1_0
)
add_halide_library(
    tq1_0_dequantize
    FROM quants.generator
    GENERATOR lookup_table_dequantize
    PARAMS family=tq1_0
)
add_halide_library(
    tq1_0_vec_dot
    FROM quants.generator
    GENERATOR lookup_table_vec_dot
    PARAMS family=tq1_0
)
# TQ2_0's quantize/dequantize kernels are GENERATOR_ARGS instantiations of
# the generic, reusable Approximation-based lookup_table_quantize/
# lookup_table_dequantize generators (see lookup_table_quant_generators.cpp
# and quant_components.h's LookupTableQuantize/PlanarBitPack).
add_halide_library(
    tq2_0_quantize
    FROM quants.generator
    GENERATOR lookup_table_quantize
    PARAMS family=tq2_0
)
add_halide_library(
    tq2_0_dequantize
    FROM quants.generator
    GENERATOR lookup_table_dequantize
    PARAMS family=tq2_0
)
add_halide_library(
    tq2_0_vec_dot
    FROM quants.generator
    GENERATOR lookup_table_vec_dot
    PARAMS family=tq2_0
)
add_halide_library(
    iq2_xxs_dequantize
    FROM quants.generator
    GENERATOR lookup_table_dequantize
    PARAMS family=iq2_xxs
)
add_halide_library(
    iq2_xxs_vec_dot
    FROM quants.generator
    GENERATOR lookup_table_vec_dot
    PARAMS family=iq2_xxs
)
add_halide_library(
    iq2_xs_dequantize
    FROM quants.generator
    GENERATOR lookup_table_dequantize
    PARAMS family=iq2_xs
)
add_halide_library(
    iq2_xs_vec_dot
    FROM quants.generator
    GENERATOR lookup_table_vec_dot
    PARAMS family=iq2_xs
)
# IQ2_S/IQ3_XXS/IQ3_S's quantize/dequantize kernels are GENERATOR_ARGS
# instantiations of the generic, reusable Approximation-based
# lookup_table_quantize/lookup_table_dequantize generators (see
# lookup_table_quant_generators.cpp and quant_components.h's
# IQ2SGridDequantize/IQ3XXSGridDequantize/IQ3SGridDequantize).
add_halide_library(
    iq2_s_quantize
    FROM quants.generator
    GENERATOR lookup_table_quantize
    PARAMS family=iq2_s
)
add_halide_library(
    iq2_s_dequantize
    FROM quants.generator
    GENERATOR lookup_table_dequantize
    PARAMS family=iq2_s
)
add_halide_library(
    iq2_s_vec_dot
    FROM quants.generator
    GENERATOR lookup_table_vec_dot
    PARAMS family=iq2_s
)
add_halide_library(
    iq3_xxs_quantize
    FROM quants.generator
    GENERATOR lookup_table_quantize
    PARAMS family=iq3_xxs
)
add_halide_library(
    iq3_xxs_dequantize
    FROM quants.generator
    GENERATOR lookup_table_dequantize
    PARAMS family=iq3_xxs
)
add_halide_library(
    iq3_xxs_vec_dot
    FROM quants.generator
    GENERATOR lookup_table_vec_dot
    PARAMS family=iq3_xxs
)
add_halide_library(
    iq3_s_quantize
    FROM quants.generator
    GENERATOR lookup_table_quantize
    PARAMS family=iq3_s
)
add_halide_library(
    iq3_s_dequantize
    FROM quants.generator
    GENERATOR lookup_table_dequantize
    PARAMS family=iq3_s
)
add_halide_library(
    iq3_s_vec_dot
    FROM quants.generator
    GENERATOR lookup_table_vec_dot
    PARAMS family=iq3_s
)
add_halide_library(
    iq1_s_dequantize
    FROM quants.generator
    GENERATOR lookup_table_dequantize
    PARAMS family=iq1_s
)
add_halide_library(
    iq1_s_vec_dot
    FROM quants.generator
    GENERATOR lookup_table_vec_dot
    PARAMS family=iq1_s
)
add_halide_library(
    iq1_m_dequantize
    FROM quants.generator
    GENERATOR lookup_table_dequantize
    PARAMS family=iq1_m
)
add_halide_library(
    iq1_m_vec_dot
    FROM quants.generator
    GENERATOR lookup_table_vec_dot
    PARAMS family=iq1_m
)
add_halide_library(f16_quantize FROM quants.generator GENERATOR f16_quantize)
add_halide_library(f16_dequantize FROM quants.generator GENERATOR f16_dequantize)
add_halide_library(bf16_quantize FROM quants.generator GENERATOR bf16_quantize)
add_halide_library(bf16_dequantize FROM quants.generator GENERATOR bf16_dequantize)
add_halide_library(q8_0_4x4_quantize_mat FROM quants.generator GENERATOR q8_0_4x4_quantize_mat)
add_halide_library(q8_0_4x8_quantize_mat FROM quants.generator GENERATOR q8_0_4x8_quantize_mat)
add_halide_library(q8_k_4x4_quantize_mat FROM quants.generator GENERATOR q8_k_4x4_quantize_mat)
add_halide_library(q8_k_4x8_quantize_mat FROM quants.generator GENERATOR q8_k_4x8_quantize_mat)

add_halide_library(
    q4_0_4x4_gemv
    FROM quants.generator
    GENERATOR repack_gemv
    PARAMS family=q4_0 n_cols=4 blocklen=4
)
add_halide_library(
    q4_0_4x8_gemv
    FROM quants.generator
    GENERATOR repack_gemv
    PARAMS family=q4_0 n_cols=4 blocklen=8
)
add_halide_library(
    q4_0_8x8_gemv
    FROM quants.generator
    GENERATOR repack_gemv
    PARAMS family=q4_0 n_cols=8 blocklen=8
)
add_halide_library(
    q8_0_4x4_gemv
    FROM quants.generator
    GENERATOR repack_gemv
    PARAMS family=q8_0 n_cols=4 blocklen=4
)
add_halide_library(
    q8_0_4x8_gemv
    FROM quants.generator
    GENERATOR repack_gemv
    PARAMS family=q8_0 n_cols=4 blocklen=8
)
add_halide_library(
    q4_0_4x4_gemm
    FROM quants.generator
    GENERATOR repack_gemm
    PARAMS family=q4_0 n_cols=4 blocklen=4
)
add_halide_library(
    q4_0_4x8_gemm
    FROM quants.generator
    GENERATOR repack_gemm
    PARAMS family=q4_0 n_cols=4 blocklen=8
)
add_halide_library(
    q4_0_8x8_gemm
    FROM quants.generator
    GENERATOR repack_gemm
    PARAMS family=q4_0 n_cols=8 blocklen=8
)
add_halide_library(
    q8_0_4x4_gemm
    FROM quants.generator
    GENERATOR repack_gemm
    PARAMS family=q8_0 n_cols=4 blocklen=4
)
add_halide_library(
    q8_0_4x8_gemm
    FROM quants.generator
    GENERATOR repack_gemm
    PARAMS family=q8_0 n_cols=4 blocklen=8
)
add_halide_library(
    iq4_nl_4x4_gemv
    FROM quants.generator
    GENERATOR repack_gemv
    PARAMS family=iq4_nl n_cols=4 blocklen=4
)
add_halide_library(
    iq4_nl_8x8_gemv
    FROM quants.generator
    GENERATOR repack_gemv
    PARAMS family=iq4_nl n_cols=8 blocklen=8
)
add_halide_library(
    mxfp4_4x4_gemv
    FROM quants.generator
    GENERATOR repack_gemv
    PARAMS family=mxfp4 n_cols=4 blocklen=4
)
add_halide_library(
    mxfp4_8x8_gemv
    FROM quants.generator
    GENERATOR repack_gemv
    PARAMS family=mxfp4 n_cols=8 blocklen=8
)
add_halide_library(
    iq4_nl_4x4_gemm
    FROM quants.generator
    GENERATOR repack_gemm
    PARAMS family=iq4_nl n_cols=4 blocklen=4
)
add_halide_library(
    iq4_nl_8x8_gemm
    FROM quants.generator
    GENERATOR repack_gemm
    PARAMS family=iq4_nl n_cols=8 blocklen=8
)
add_halide_library(
    mxfp4_4x4_gemm
    FROM quants.generator
    GENERATOR repack_gemm
    PARAMS family=mxfp4 n_cols=4 blocklen=4
)
add_halide_library(
    mxfp4_8x8_gemm
    FROM quants.generator
    GENERATOR repack_gemm
    PARAMS family=mxfp4 n_cols=8 blocklen=8
)
add_halide_library(
    q4_k_8x4_gemv
    FROM quants.generator
    GENERATOR repack_gemv
    PARAMS family=q4_k n_cols=8 blocklen=4
)
add_halide_library(
    q4_k_8x8_gemv
    FROM quants.generator
    GENERATOR repack_gemv
    PARAMS family=q4_k n_cols=8 blocklen=8
)
add_halide_library(
    q4_k_8x4_gemm
    FROM quants.generator
    GENERATOR repack_gemm
    PARAMS family=q4_k n_cols=8 blocklen=4
)
add_halide_library(
    q4_k_8x8_gemm
    FROM quants.generator
    GENERATOR repack_gemm
    PARAMS family=q4_k n_cols=8 blocklen=8
)
add_halide_library(
    q5_k_8x4_gemv
    FROM quants.generator
    GENERATOR repack_gemv
    PARAMS family=q5_k n_cols=8 blocklen=4
)
add_halide_library(
    q5_k_8x8_gemv
    FROM quants.generator
    GENERATOR repack_gemv
    PARAMS family=q5_k n_cols=8 blocklen=8
)
add_halide_library(
    q5_k_8x4_gemm
    FROM quants.generator
    GENERATOR repack_gemm
    PARAMS family=q5_k n_cols=8 blocklen=4
)
add_halide_library(
    q5_k_8x8_gemm
    FROM quants.generator
    GENERATOR repack_gemm
    PARAMS family=q5_k n_cols=8 blocklen=8
)
add_halide_library(
    q6_k_8x4_gemv
    FROM quants.generator
    GENERATOR repack_gemv
    PARAMS family=q6_k n_cols=8 blocklen=4
)
add_halide_library(
    q6_k_8x8_gemv
    FROM quants.generator
    GENERATOR repack_gemv
    PARAMS family=q6_k n_cols=8 blocklen=8
)
add_halide_library(
    q6_k_8x4_gemm
    FROM quants.generator
    GENERATOR repack_gemm
    PARAMS family=q6_k n_cols=8 blocklen=4
)
add_halide_library(
    q6_k_8x8_gemm
    FROM quants.generator
    GENERATOR repack_gemm
    PARAMS family=q6_k n_cols=8 blocklen=8
)
add_halide_library(
    q2_k_8x8_gemv
    FROM quants.generator
    GENERATOR repack_gemv
    PARAMS family=q2_k n_cols=8 blocklen=8
)
add_halide_library(
    q2_k_8x8_gemm
    FROM quants.generator
    GENERATOR repack_gemm
    PARAMS family=q2_k n_cols=8 blocklen=8
)

# ggml_extern_quantize.cpp implements the extern-stage C functions that
# several of the quantize pipelines above call out to -- see that file for
# why. It's the one source in this library that depends on GGML, hence the
# extra ggml::ggml link below (every other source here is GGML-independent).
add_library(ggml_quants_halide ggml_quants.cpp ggml_extern_quantize.cpp)
target_link_libraries(
    ggml_quants_halide
    PUBLIC
    q4_0_quantize q4_0_dequantize q4_0_vec_dot q4_1_quantize q4_1_dequantize q4_1_vec_dot
    q5_0_quantize q5_0_dequantize q5_0_vec_dot q5_1_quantize q5_1_dequantize q5_1_vec_dot
    q8_0_quantize q8_0_dequantize q8_0_vec_dot q8_1_quantize q8_k_quantize q2_k_quantize
    q2_k_dequantize q2_k_vec_dot q6_k_quantize q6_k_dequantize q6_k_vec_dot q4_k_quantize
    q4_k_dequantize q4_k_vec_dot q5_k_quantize q5_k_dequantize q5_k_vec_dot q3_k_quantize
    q3_k_dequantize q3_k_vec_dot q1_0_quantize q1_0_dequantize q1_0_vec_dot mxfp4_quantize
    mxfp4_dequantize mxfp4_vec_dot nvfp4_quantize nvfp4_dequantize nvfp4_vec_dot iq4_nl_quantize
    iq4_nl_dequantize iq4_nl_vec_dot iq4_xs_quantize iq4_xs_dequantize iq4_xs_vec_dot tq1_0_quantize
    tq1_0_dequantize tq1_0_vec_dot tq2_0_quantize tq2_0_dequantize tq2_0_vec_dot iq2_xxs_dequantize
    iq2_xxs_vec_dot iq2_xs_dequantize iq2_xs_vec_dot iq2_s_quantize iq2_s_dequantize iq2_s_vec_dot
    iq3_xxs_quantize iq3_xxs_dequantize iq3_xxs_vec_dot iq3_s_quantize iq3_s_dequantize
    iq3_s_vec_dot iq1_s_dequantize iq1_s_vec_dot iq1_m_dequantize iq1_m_vec_dot f16_quantize
    f16_dequantize bf16_quantize bf16_dequantize q8_0_4x4_quantize_mat q8_0_4x8_quantize_mat
    q8_k_4x4_quantize_mat q8_k_4x8_quantize_mat q4_0_4x4_gemv q4_0_4x8_gemv q4_0_8x8_gemv
    q8_0_4x4_gemv q8_0_4x8_gemv q4_0_4x4_gemm q4_0_4x8_gemm q4_0_8x8_gemm q8_0_4x4_gemm
    q8_0_4x8_gemm iq4_nl_4x4_gemv iq4_nl_8x8_gemv mxfp4_4x4_gemv mxfp4_8x8_gemv iq4_nl_4x4_gemm
    iq4_nl_8x8_gemm mxfp4_4x4_gemm mxfp4_8x8_gemm q4_k_8x4_gemv q4_k_8x8_gemv q4_k_8x4_gemm
    q4_k_8x8_gemm q5_k_8x4_gemv q5_k_8x8_gemv q5_k_8x4_gemm q5_k_8x8_gemm q6_k_8x4_gemv
    q6_k_8x8_gemv q6_k_8x4_gemm q6_k_8x8_gemm q2_k_8x8_gemv q2_k_8x8_gemm ggml::ggml
)
target_include_directories(ggml_quants_halide PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Standalone correctness checks against GGML's own reference, run before (and
# independently of) wiring these into kernel-bench as providers.
foreach (t IN ITEMS
    q4_0
    q4_1
    q5_0
    q5_1
    q8_0
    q8_1
    q8_k
    q2_k
    q6_k
    q4_k
    q5_k
    q3_k
    q1_0
    mxfp4
    nvfp4
    iq4_nl
    iq4_xs
    tq1_0
    tq2_0
    iq2_xxs
    iq2_xs
    iq2_s
    iq3_xxs
    iq3_s
    iq1_s
    iq1_m
    f16
    bf16
)
    add_executable(test_${t} test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE ggml_quants_halide ggml::ggml)
    target_include_directories(test_${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    add_test(NAME ${t}_roundtrip COMMAND test_${t})
    set_tests_properties(${t}_roundtrip PROPERTIES PASS_REGULAR_EXPRESSION "Success!")
endforeach ()
