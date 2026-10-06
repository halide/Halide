# The format table. One row per format (a scheme in schemes/schemes.h):
#
#   type  ops  [target-features]
#
# `ops`: codec (quantize + dequantize of a row, checked bit-exact against
# GGML). Each generated library has a checked variant (default target: asserts
# and bounds queries on; used by --check and tests) and a bench variant
# (GGML_BENCH_FEATURES; what gets timed), both registered with the harness
# (harness/halide_providers.cpp, harness/codec.cpp).
set(GGML_FORMATS "q4_0 codec" "q8_0 codec")

# Plumbing example kernel (weight act ops features), until mul_mat lands.
set(GGML_HALIDE_KERNELS "example_q8_0_f32 example q8_0 f32 vec_dot,mul_mat -")
set(GGML_BENCH_FEATURES no_asserts no_bounds_query)

set(GGML_HALIDE_LIBRARIES "")
set(registry "")

function(_ggml_library name gen features)
    set(features_checked "")
    if (NOT features STREQUAL "-")
        string(REPLACE "," ";" features_checked "${features}")
    endif ()
    set(features_bench ${features_checked} ${GGML_BENCH_FEATURES})
    foreach (variant IN ITEMS checked bench)
        add_halide_library(
            ${name}_${variant}
            FROM ggml.generators
            GENERATOR ${gen}
            FEATURES ${features_${variant}}
            PARAMS ${ARGN}
            USE_RUNTIME ggml_halide_runtime
        )
        list(APPEND GGML_HALIDE_LIBRARIES ${name}_${variant})
        string(APPEND registry "#include \"${name}_${variant}.h\"\n")
    endforeach ()
    set(GGML_HALIDE_LIBRARIES "${GGML_HALIDE_LIBRARIES}" PARENT_SCOPE)
    set(registry "${registry}" PARENT_SCOPE)
endfunction()

foreach (row IN LISTS GGML_FORMATS)
    separate_arguments(row UNIX_COMMAND "${row}")
    list(POP_FRONT row type ops features)
    if (NOT features)
        set(features "-")
    endif ()
    string(REPLACE "," ";" ops "${ops}")
    if ("codec" IN_LIST ops)
        _ggml_library(${type}_quantize codec ${features} type=${type} quantize=true)
        _ggml_library(${type}_dequantize codec ${features} type=${type} quantize=false)
        string(APPEND registry "GQ_CODEC(${type})\n")
    endif ()
endforeach ()

foreach (row IN LISTS GGML_HALIDE_KERNELS)
    separate_arguments(row UNIX_COMMAND "${row}")
    list(POP_FRONT row name gen wt at ops features)
    _ggml_library(${name} ${gen} ${features} ${row})
    string(REPLACE "," "|GQ_" ops "GQ_${ops}")
    string(APPEND registry "GQ_HALIDE(${name}, ${wt}, ${at}, ${ops}, \"${features}\")\n")
endforeach ()
file(CONFIGURE OUTPUT halide_kernels.inc CONTENT "${registry}")
