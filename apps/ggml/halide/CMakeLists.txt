add_halide_generator(
    quants.generator
    SOURCES
    q4_0_generators.cpp
    q4_1_generators.cpp
    q5_0_generators.cpp
    q5_1_generators.cpp
    q8_0_generators.cpp
    q8_1_generators.cpp
    q8_k_generators.cpp
    q2_k_generators.cpp
    q6_k_generators.cpp
    q4_k_generators.cpp
    q5_k_generators.cpp
    q3_k_generators.cpp
    q1_0_generators.cpp
    mxfp4_generators.cpp
    nvfp4_generators.cpp
    iq4_nl_generators.cpp
    iq4_xs_generators.cpp
    tq1_0_generators.cpp
    tq2_0_generators.cpp
    iq2_xxs_generators.cpp
    iq2_xs_generators.cpp
    iq2_s_generators.cpp
    iq3_xxs_generators.cpp
    iq3_s_generators.cpp
    iq1_s_generators.cpp
    iq1_m_generators.cpp
    f16_generators.cpp
    bf16_generators.cpp
    repack_quantize_mat_generators.cpp
)

add_halide_library(q4_0_quantize FROM quants.generator GENERATOR q4_0_quantize)
add_halide_library(q4_0_dequantize FROM quants.generator GENERATOR q4_0_dequantize)
add_halide_library(q4_0_vec_dot FROM quants.generator GENERATOR q4_0_vec_dot)
add_halide_library(q4_1_quantize FROM quants.generator GENERATOR q4_1_quantize)
add_halide_library(q4_1_dequantize FROM quants.generator GENERATOR q4_1_dequantize)
add_halide_library(q4_1_vec_dot FROM quants.generator GENERATOR q4_1_vec_dot)
add_halide_library(q5_0_quantize FROM quants.generator GENERATOR q5_0_quantize)
add_halide_library(q5_0_dequantize FROM quants.generator GENERATOR q5_0_dequantize)
add_halide_library(q5_0_vec_dot FROM quants.generator GENERATOR q5_0_vec_dot)
add_halide_library(q5_1_quantize FROM quants.generator GENERATOR q5_1_quantize)
add_halide_library(q5_1_dequantize FROM quants.generator GENERATOR q5_1_dequantize)
add_halide_library(q5_1_vec_dot FROM quants.generator GENERATOR q5_1_vec_dot)
add_halide_library(q8_0_quantize FROM quants.generator GENERATOR q8_0_quantize)
add_halide_library(q8_0_dequantize FROM quants.generator GENERATOR q8_0_dequantize)
add_halide_library(q8_0_vec_dot FROM quants.generator GENERATOR q8_0_vec_dot)
add_halide_library(q8_1_quantize FROM quants.generator GENERATOR q8_1_quantize)
add_halide_library(q8_k_quantize FROM quants.generator GENERATOR q8_k_quantize)
add_halide_library(q2_k_quantize FROM quants.generator GENERATOR q2_k_quantize)
add_halide_library(q2_k_dequantize FROM quants.generator GENERATOR q2_k_dequantize)
add_halide_library(q2_k_vec_dot FROM quants.generator GENERATOR q2_k_vec_dot)
add_halide_library(q6_k_quantize FROM quants.generator GENERATOR q6_k_quantize)
add_halide_library(q6_k_dequantize FROM quants.generator GENERATOR q6_k_dequantize)
add_halide_library(q6_k_vec_dot FROM quants.generator GENERATOR q6_k_vec_dot)
add_halide_library(q4_k_quantize FROM quants.generator GENERATOR q4_k_quantize)
add_halide_library(q4_k_dequantize FROM quants.generator GENERATOR q4_k_dequantize)
add_halide_library(q4_k_vec_dot FROM quants.generator GENERATOR q4_k_vec_dot)
add_halide_library(q5_k_quantize FROM quants.generator GENERATOR q5_k_quantize)
add_halide_library(q5_k_dequantize FROM quants.generator GENERATOR q5_k_dequantize)
add_halide_library(q5_k_vec_dot FROM quants.generator GENERATOR q5_k_vec_dot)
add_halide_library(q3_k_quantize FROM quants.generator GENERATOR q3_k_quantize)
add_halide_library(q3_k_dequantize FROM quants.generator GENERATOR q3_k_dequantize)
add_halide_library(q3_k_vec_dot FROM quants.generator GENERATOR q3_k_vec_dot)
add_halide_library(q1_0_quantize FROM quants.generator GENERATOR q1_0_quantize)
add_halide_library(q1_0_dequantize FROM quants.generator GENERATOR q1_0_dequantize)
add_halide_library(q1_0_vec_dot FROM quants.generator GENERATOR q1_0_vec_dot)
add_halide_library(mxfp4_quantize FROM quants.generator GENERATOR mxfp4_quantize)
add_halide_library(mxfp4_dequantize FROM quants.generator GENERATOR mxfp4_dequantize)
add_halide_library(mxfp4_vec_dot FROM quants.generator GENERATOR mxfp4_vec_dot)
add_halide_library(nvfp4_quantize FROM quants.generator GENERATOR nvfp4_quantize)
add_halide_library(nvfp4_dequantize FROM quants.generator GENERATOR nvfp4_dequantize)
add_halide_library(nvfp4_vec_dot FROM quants.generator GENERATOR nvfp4_vec_dot)
add_halide_library(iq4_nl_quantize FROM quants.generator GENERATOR iq4_nl_quantize)
add_halide_library(iq4_nl_dequantize FROM quants.generator GENERATOR iq4_nl_dequantize)
add_halide_library(iq4_nl_vec_dot FROM quants.generator GENERATOR iq4_nl_vec_dot)
add_halide_library(iq4_xs_quantize FROM quants.generator GENERATOR iq4_xs_quantize)
add_halide_library(iq4_xs_dequantize FROM quants.generator GENERATOR iq4_xs_dequantize)
add_halide_library(iq4_xs_vec_dot FROM quants.generator GENERATOR iq4_xs_vec_dot)
add_halide_library(tq1_0_quantize FROM quants.generator GENERATOR tq1_0_quantize)
add_halide_library(tq1_0_dequantize FROM quants.generator GENERATOR tq1_0_dequantize)
add_halide_library(tq1_0_vec_dot FROM quants.generator GENERATOR tq1_0_vec_dot)
add_halide_library(tq2_0_quantize FROM quants.generator GENERATOR tq2_0_quantize)
add_halide_library(tq2_0_dequantize FROM quants.generator GENERATOR tq2_0_dequantize)
add_halide_library(tq2_0_vec_dot FROM quants.generator GENERATOR tq2_0_vec_dot)
add_halide_library(iq2_xxs_dequantize FROM quants.generator GENERATOR iq2_xxs_dequantize)
add_halide_library(iq2_xxs_vec_dot FROM quants.generator GENERATOR iq2_xxs_vec_dot)
add_halide_library(iq2_xs_dequantize FROM quants.generator GENERATOR iq2_xs_dequantize)
add_halide_library(iq2_xs_vec_dot FROM quants.generator GENERATOR iq2_xs_vec_dot)
add_halide_library(iq2_s_quantize FROM quants.generator GENERATOR iq2_s_quantize)
add_halide_library(iq2_s_dequantize FROM quants.generator GENERATOR iq2_s_dequantize)
add_halide_library(iq2_s_vec_dot FROM quants.generator GENERATOR iq2_s_vec_dot)
add_halide_library(iq3_xxs_quantize FROM quants.generator GENERATOR iq3_xxs_quantize)
add_halide_library(iq3_xxs_dequantize FROM quants.generator GENERATOR iq3_xxs_dequantize)
add_halide_library(iq3_xxs_vec_dot FROM quants.generator GENERATOR iq3_xxs_vec_dot)
add_halide_library(iq3_s_quantize FROM quants.generator GENERATOR iq3_s_quantize)
add_halide_library(iq3_s_dequantize FROM quants.generator GENERATOR iq3_s_dequantize)
add_halide_library(iq3_s_vec_dot FROM quants.generator GENERATOR iq3_s_vec_dot)
add_halide_library(iq1_s_dequantize FROM quants.generator GENERATOR iq1_s_dequantize)
add_halide_library(iq1_s_vec_dot FROM quants.generator GENERATOR iq1_s_vec_dot)
add_halide_library(iq1_m_dequantize FROM quants.generator GENERATOR iq1_m_dequantize)
add_halide_library(iq1_m_vec_dot FROM quants.generator GENERATOR iq1_m_vec_dot)
add_halide_library(f16_quantize FROM quants.generator GENERATOR f16_quantize)
add_halide_library(f16_dequantize FROM quants.generator GENERATOR f16_dequantize)
add_halide_library(bf16_quantize FROM quants.generator GENERATOR bf16_quantize)
add_halide_library(bf16_dequantize FROM quants.generator GENERATOR bf16_dequantize)
add_halide_library(q8_0_4x4_quantize_mat FROM quants.generator GENERATOR q8_0_4x4_quantize_mat)
add_halide_library(q8_0_4x8_quantize_mat FROM quants.generator GENERATOR q8_0_4x8_quantize_mat)
add_halide_library(q8_k_4x4_quantize_mat FROM quants.generator GENERATOR q8_k_4x4_quantize_mat)
add_halide_library(q8_k_4x8_quantize_mat FROM quants.generator GENERATOR q8_k_4x8_quantize_mat)

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
    q8_k_4x4_quantize_mat q8_k_4x8_quantize_mat ggml::ggml
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
