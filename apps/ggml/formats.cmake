# The format table. One row per format (schemes/schemes.h):
#
#   type  ops  [acts  [target-features]]
#
# `type`: <ggml type>[.<codes>][.<rows>x<chunk>]. The optional codes pick the
# integer encoding (i4: two's-complement nibbles; default: GGML's offset
# binary), the layout interleaves `rows` rows' records in `chunk`-byte pieces
# (as GGML's repack q4_0_4x8 etc.; default: one row per record, AoS); its
# weights are checked against GGML's repack (harness/repack.cpp).
#
# `ops`: codec (quantize + dequantize of a row, checked bit-exact against
# GGML, also with the scaled codes kernels may pick: checked variant only),
# vec_dot and mul_mat (kernels/matmul.cpp with weight=type op=<op>, one
# library per act in `acts`). An act is <storage>[:<compute>]: f32:q8_0 takes
# f32 activations and quantizes them to q8_0 inside the kernel (as GGML's CPU
# paths do); its library is <type>_f32_to_q8_0_<op>. Each generated library
# has a checked variant (default target: asserts and bounds queries on; used
# by --check and tests) and a bench variant
# (GGML_BENCH_FEATURES; what gets timed), both registered with the harness
# (harness/halide_providers.cpp, harness/codec.cpp).
set(GGML_FORMATS
    "q4_0 codec,vec_dot,mul_mat q8_0,f16,f32,f32:q8_0"
    "q8_0 codec,vec_dot q8_0,f16,f32"
    "q4_0.i4.4x4 codec"
    "q4_0.i4.4x8 codec,mul_mat q8_0,f32:q8_0,f32:q8_0.4x8"
    "q4_0.i4.8x8 codec"
)
set(GGML_BENCH_FEATURES no_asserts no_bounds_query)

set(GGML_HALIDE_LIBRARIES "")
set(registry "")

# Builds the checked and bench variants (`_ggml_variants`, if set: those).
function(_ggml_library name gen features)
    if (NOT _ggml_variants)
        set(_ggml_variants checked bench)
    endif ()
    set(features_checked "")
    if (NOT features STREQUAL "-")
        string(REPLACE "," ";" features_checked "${features}")
    endif ()
    set(features_bench ${features_checked} ${GGML_BENCH_FEATURES})
    foreach (variant IN LISTS _ggml_variants)
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
    list(POP_FRONT row type ops acts features)
    if (NOT features)
        set(features "-")
    endif ()
    string(REPLACE "," ";" ops "${ops}")
    string(MAKE_C_IDENTIFIER "${type}" id)
    if ("codec" IN_LIST ops)
        _ggml_library(${id}_quantize codec ${features} type=${type} quantize=true)
        _ggml_library(${id}_dequantize codec ${features} type=${type} quantize=false)
        set(_ggml_variants checked)
        _ggml_library(
            ${id}_quantize_scaled codec ${features} type=${type} quantize=true scaled=true
        )
        _ggml_library(
            ${id}_dequantize_scaled codec ${features} type=${type} quantize=false scaled=true
        )
        unset(_ggml_variants)
        string(APPEND registry "GQ_CODEC(${id}, \"${type}\")\n")
    endif ()
    string(REPLACE "," ";" acts "${acts}")
    foreach (op IN ITEMS vec_dot mul_mat)
        if (NOT op IN_LIST ops)
            continue ()
        endif ()
        foreach (act IN LISTS acts)
            string(REPLACE ":" ";" at "${act}")
            list(GET at 0 storage)
            list(GET at -1 compute)
            string(REPLACE ":" "_to_" name "${id}_${act}_${op}")
            string(MAKE_C_IDENTIFIER "${name}" name)
            _ggml_library(${name} matmul ${features} weight=${type} act=${act} op=${op})
            string(APPEND registry
                "GQ_HALIDE(${name}, \"${type}\", ${storage}, ${compute}, GQ_${op}, \"${features}\")\n"
            )
        endforeach ()
    endforeach ()
endforeach ()
file(CONFIGURE OUTPUT halide_kernels.inc CONTENT "${registry}")
