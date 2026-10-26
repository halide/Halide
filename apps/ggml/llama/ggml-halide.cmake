# The Halide kernels in llama.cpp (see README.md). Included by the patched
# ggml-cpu and ggml/src CMakeLists.txt (GGML_CPU_HALIDE: this file's path);
# reads:
#   GGML_HALIDE_KERNEL_DIR  a build of apps/ggml: its lib<kernel>.a, <kernel>.h
#                           and libggml_halide_runtime.a
#   GGML_HALIDE_INCLUDE_DIR Halide's include directory (HalideRuntime.h)
#   GGML_HALIDE_VEC_DOTS    weight:act pairs, e.g. q4_0:q8_0;q4_K:q8_K (their
#                           <weight>_<act>_vec_dot_bench kernels)
#   GGML_HALIDE_MUL_MATS    the same, for <weight>_<act>_mul_mat_bench
#   GGML_HALIDE_BACKEND_MUL_MATS  the same, for the HALIDE backend's
#                           <weight>_f32_to_<act>_mul_mat_bench
set(GGML_HALIDE_KERNEL_DIR "" CACHE PATH "apps/ggml build directory")
set(GGML_HALIDE_INCLUDE_DIR "" CACHE PATH "Halide's include directory")
set(GGML_HALIDE_VEC_DOTS "q4_0:q8_0;q8_0:q8_0;q4_K:q8_K;q6_K:q8_K;iq4_xs:q8_K"
    CACHE STRING "weight:act vec_dot kernels"
)
set(GGML_HALIDE_MUL_MATS "q4_0:q8_0;q8_0:q8_0;q4_K:q8_K;q6_K:q8_K;iq4_xs:q8_K"
    CACHE STRING "weight:act mul_mat kernels"
)
set(GGML_HALIDE_BACKEND_MUL_MATS "q4_0:q8_0;q8_0:q8_0;q4_K:q8_K;q6_K:q8_K;iq4_xs:q8_K"
    CACHE STRING "weight:act backend mul_mat kernels"
)

# Writes ${header}: the kernels' headers, and per list (<macro>:<pairs var>:
# <kernel name pattern, W and A for the types>) a #define <macro>S of
# <macro>(<kernel>, <WEIGHT>, <ACT>) per pair; sets ${out_libs} to their archives and the runtime.
function(_ggml_halide_kernels header out_libs)
    set(dir "${GGML_HALIDE_KERNEL_DIR}")
    set(includes "")
    set(defines "")
    set(ls "")
    foreach (spec IN LISTS ARGN)
        string(REPLACE ":" ";" spec "${spec}")
        list(GET spec 0 macro)
        list(GET spec 1 var)
        list(GET spec 2 pattern)
        set(entries "")
        foreach (pair IN LISTS ${var})
            string(REPLACE ":" ";" wa "${pair}")
            list(GET wa 0 w)
            list(GET wa 1 a)
            string(REPLACE "W" "${w}" k "${pattern}")
            string(REPLACE "A" "${a}" k "${k}")
            string(TOUPPER "${w}" W)
            string(TOUPPER "${a}" A)
            string(APPEND includes "#include \"${dir}/${k}.h\"\n")
            string(APPEND entries " \\\n    ${macro}(${k}, ${W}, ${A})")
            list(APPEND ls "${dir}/lib${k}.a")
        endforeach ()
        string(APPEND defines "#define ${macro}S${entries}\n")
    endforeach ()
    file(CONFIGURE OUTPUT "${header}" CONTENT "#pragma once\n${includes}\n${defines}")
    list(APPEND ls "${dir}/libggml_halide_runtime.a")
    set(${out_libs} "${ls}" PARENT_SCOPE)
endfunction()

function(_ggml_halide_link target libs)
    target_link_libraries(${target} PRIVATE ${libs})
    if (APPLE)
        # (the runtime's Metal support)
        target_link_libraries(${target} PRIVATE "-framework Metal" "-framework Foundation")
    endif ()
endfunction()

# ggml-cpu: GGML_HALIDE=vec_dot and mul_mat (ggml-halide-cpu.cpp)
function(ggml_halide_add_to target)
    set(gen "${CMAKE_CURRENT_BINARY_DIR}/ggml_halide")
    set(libs "")
    _ggml_halide_kernels(
        "${gen}/ggml_halide_kernels.h" libs
        "GGML_HALIDE_VEC_DOT:GGML_HALIDE_VEC_DOTS:W_A_vec_dot_bench"
        "GGML_HALIDE_MUL_MAT:GGML_HALIDE_MUL_MATS:W_A_mul_mat_bench"
    )
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ggml-halide-cpu.cpp")
    # (ggml-cpu's internal headers: traits.h, ggml-impl.h)
    target_include_directories(
        ${target}
        PRIVATE
        "${gen}"
        "${GGML_HALIDE_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
    )
    _ggml_halide_link(${target} "${libs}")
endfunction()

# The HALIDE backend, GGML_HALIDE=backend (ggml-halide-backend.cpp), with the
# app's thread pool. Called from ggml/src's CMakeLists.txt.
function(ggml_halide_add_backend)
    set(gen "${CMAKE_CURRENT_BINARY_DIR}/ggml_halide_backend")
    set(libs "")
    _ggml_halide_kernels(
        "${gen}/ggml_halide_backend_kernels.h" libs
        "GGML_HALIDE_BACKEND_MUL_MAT:GGML_HALIDE_BACKEND_MUL_MATS:W_f32_to_A_mul_mat_bench"
    )
    set(rt "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../runtime")
    ggml_add_backend_library(
        ggml-halide "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ggml-halide-backend.cpp"
        "${rt}/thread_pool.cpp"
    )
    target_include_directories(
        ggml-halide
        PRIVATE
        "${gen}"
        "${GGML_HALIDE_INCLUDE_DIR}"
        "${rt}"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../include"
    )
    target_link_libraries(ggml-halide PRIVATE ggml-cpu)
    _ggml_halide_link(ggml-halide "${libs}")
    target_compile_definitions(ggml PUBLIC GGML_USE_HALIDE_BACKEND)
endfunction()
