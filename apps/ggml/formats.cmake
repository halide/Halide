# The kernel table. One row per Halide kernel; each row builds a checked
# variant (default target: asserts and bounds queries on; used by --check and
# tests) and a bench variant (GGML_BENCH_FEATURES; what gets timed), and
# registers both with the harness (see harness/halide_providers.cpp).
#
#   name  generator  weight  act  ops  target-features  [generator params...]
#
# `act` is a ggml type name (q8_0, q8_K, f16, f32); `ops` is vec_dot, mul_mat,
# or both joined by a comma; `target-features` is "-" or e.g. "metal".
set(GGML_HALIDE_KERNELS "example_q8_0_f32 example q8_0 f32 vec_dot,mul_mat -")
set(GGML_BENCH_FEATURES no_asserts no_bounds_query)

set(GGML_HALIDE_LIBRARIES "")
set(registry "")
foreach (row IN LISTS GGML_HALIDE_KERNELS)
    separate_arguments(row UNIX_COMMAND "${row}")
    list(POP_FRONT row name gen wt at ops features)
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
            PARAMS ${row}
            USE_RUNTIME ggml_halide_runtime
        )
        list(APPEND GGML_HALIDE_LIBRARIES ${name}_${variant})
    endforeach ()
    string(REPLACE "," "|GQ_" ops "GQ_${ops}")
    string(APPEND registry
        "#include \"${name}_checked.h\"\n#include \"${name}_bench.h\"\n"
        "GQ_HALIDE(${name}, ${wt}, ${at}, ${ops}, \"${features}\")\n"
    )
endforeach ()
file(CONFIGURE OUTPUT halide_kernels.inc CONTENT "${registry}")
