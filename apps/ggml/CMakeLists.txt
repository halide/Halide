cmake_minimum_required(VERSION 3.28)
project(ggml)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# GGML is expected to be provided externally -- in this tree, via the
# apps/vcpkg/ports/ggml overlay port (see its portfile.cmake). GGML ships its
# own CMake package config (installed to <prefix>/share/ggml/ by vcpkg's
# vcpkg_cmake_config_fixup), so no Find module is needed here.
find_package(ggml CONFIG REQUIRED)

# Halide is already found once by the parent apps/CMakeLists.txt when this app
# is built as part of the full apps/ tree (a no-op re-find in that case); this
# call makes apps/ggml also independently configurable/buildable on its own,
# matching every other app's convention (see e.g. apps/blur/CMakeLists.txt).
find_package(Halide REQUIRED)

# halide/ contains a from-scratch Halide reimplementation of GGML's Q4_0
# quantize/dequantize kernels (ggml_quants_halide), benchmarked against
# GGML's own reference by providers/halide_provider.cpp below.
add_subdirectory(halide)

add_executable(
    kernel-bench
    src/main.cpp
    src/report.cpp
    src/bench_quantize.cpp
    src/bench_dequantize.cpp
    src/bench_vecdot.cpp
    src/bench_repack.cpp
    providers/ggml_provider.cpp
    providers/halide_provider.cpp
)

target_include_directories(kernel-bench PRIVATE include providers src halide)

target_link_libraries(kernel-bench PRIVATE ggml::ggml ggml_quants_halide)

# GGML_VERSION isn't exposed through any runtime API, but ggml-config.cmake
# sets it as a plain CMake variable (baked in from the exporting build's own
# GGML_VERSION), so this is exactly the version of the library we just linked
# against -- reliable without touching GGML internals.
if (DEFINED GGML_VERSION)
    target_compile_definitions(kernel-bench PRIVATE KERNEL_BENCH_GGML_VERSION="${GGML_VERSION}")
endif ()

# ggml-config.cmake.in only creates ggml::<backend> import targets
# (including ggml::ggml-cpu) when GGML was built with GGML_BACKEND_DL=OFF
# (the default) -- see its `if (NOT GGML_BACKEND_DL)` guard. In DL mode the
# CPU backend is a runtime-loaded module with no link-time target at all, and
# the private ABI symbols this tool depends on (see
# providers/ggml_internal_abi.h) are then unreachable through the CMake
# package. Fall back to locating the library file directly by name; fail
# loudly if that isn't possible either, rather than producing a mysterious
# link error.
if (NOT TARGET ggml::ggml-cpu)
    find_library(
        GGML_CPU_DL_LIB
        NAMES ggml-cpu
        HINTS "${ggml_LIB_DIR}" "${ggml_LIB_DIR}/ggml"
        PATH_SUFFIXES lib lib/ggml bin
    )
    if (GGML_CPU_DL_LIB)
        message(
            STATUS "kernel-bench: linking ggml-cpu directly (GGML_BACKEND_DL build): ${GGML_CPU_DL_LIB}"
        )
        target_link_libraries(kernel-bench PRIVATE "${GGML_CPU_DL_LIB}")
    else ()
        message(
            FATAL_ERROR "kernel-bench: could not find ggml::ggml-cpu or a standalone ggml-cpu library. "
            "This GGML install appears to have been built with -DGGML_BACKEND_DL=ON, which "
            "loads the CPU backend as a runtime module instead of linking it -- kernel-bench "
            "needs to link directly against its internal symbols. Rebuild GGML with "
            "-DGGML_BACKEND_DL=OFF (the default) and reinstall."
        )
    endif ()
endif ()
