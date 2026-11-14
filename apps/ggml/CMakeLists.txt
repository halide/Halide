cmake_minimum_required(VERSION 3.28)
project(ggml_app)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

find_package(ggml CONFIG)
if (NOT ggml_FOUND)
    message(WARNING "Could NOT find ggml; skipping apps/ggml")
    return()
endif ()
find_package(Halide REQUIRED)

add_halide_generator(
    ggml.generators
    SOURCES kernels/codec.cpp kernels/matmul.cpp
    LINK_LIBRARIES Halide::Tools
)
target_include_directories(ggml.generators PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
add_halide_runtime(ggml_halide_runtime)

# The app's copy of Halide's thread pool (runtime/README.md). Its strong
# definitions override the runtime's weak ones; an OBJECT library, so that they
# are always linked.
add_library(ggml_thread_pool OBJECT runtime/thread_pool.cpp)
target_include_directories(ggml_thread_pool PUBLIC runtime)
target_link_libraries(ggml_thread_pool PUBLIC Halide::Runtime)

# The Metal libraries embed a metallib compiled at build time by Apple's
# Metal toolchain (Xcode's, or `xcodebuild -downloadComponent MetalToolchain`)
# instead of their Metal source, which the runtime would compile at first load.
# -fno-fast-math as the runtime's source compile (setFastMathEnabled: NO); -w:
# the generated source's unused-variable warnings.
set(_ggml_metal_tools NO)
if (APPLE)
    execute_process(
        COMMAND xcrun -sdk macosx metal --version
        RESULT_VARIABLE _metal_rc
        OUTPUT_QUIET ERROR_QUIET
    )
    execute_process(
        COMMAND xcrun -sdk macosx -f metallib
        RESULT_VARIABLE _metallib_rc
        OUTPUT_QUIET ERROR_QUIET
    )
    if (_metal_rc EQUAL 0 AND _metallib_rc EQUAL 0)
        set(_ggml_metal_tools YES)
    endif ()
endif ()
option(GGML_HALIDE_METALLIB
    "Embed compiled metallibs, not Metal source, in the Metal libraries" ${_ggml_metal_tools}
)
set(GGML_HALIDE_METAL_ENV "")
if (GGML_HALIDE_METALLIB)
    set(GGML_HALIDE_METAL_ENV
        "GGML_METAL_COMPILER=xcrun -sdk macosx metal -fno-fast-math -w"
        "GGML_METAL_LINKER=xcrun -sdk macosx metallib"
    )
endif ()

include(formats.cmake)

add_executable(
    ggml-quant-bench
    harness/bench.cpp
    harness/codec.cpp
    harness/ggml_providers.cpp
    harness/halide_providers.cpp
    harness/main.cpp
    harness/oracle.cpp
    harness/repack.cpp
    harness/shapes.cpp
    harness/trusted.cpp
)
target_include_directories(ggml-quant-bench PRIVATE harness "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(ggml-quant-bench PRIVATE ggml::ggml ggml_thread_pool ${GGML_HALIDE_LIBRARIES})
# The oracle's products run on the system's BLAS, through its C interface:
# Accelerate's own header on macOS, elsewhere cblas.h (which e.g. Fedora
# installs in a directory per implementation).
find_package(BLAS REQUIRED)
target_link_libraries(ggml-quant-bench PRIVATE BLAS::BLAS)
if (NOT APPLE)
    find_path(GGML_CBLAS_INCLUDE_DIR cblas.h PATH_SUFFIXES flexiblas openblas REQUIRED)
    target_include_directories(ggml-quant-bench PRIVATE "${GGML_CBLAS_INCLUDE_DIR}")
endif ()
if (GGML_CPU_KLEIDIAI)
    # KleidiAI's packers, the reference of the qsi8cx and qai8dx codecs
    target_compile_definitions(ggml-quant-bench PRIVATE GQ_KLEIDIAI)
endif ()
if (GGML_CUDA)
    # CUPTI's launch callbacks name the kernels GGML's CUDA mul_mat runs
    # (ggml-cuda's path column: mmq, mmvq, mmvf, mmf or cuBLAS)
    find_package(CUDAToolkit)
    if (TARGET CUDA::cupti)
        target_link_libraries(ggml-quant-bench PRIVATE CUDA::cupti)
        target_compile_definitions(ggml-quant-bench PRIVATE GQ_CUPTI)
    endif ()
endif ()

add_test(NAME ggml_check COMMAND ggml-quant-bench --check --shapes smoke)
set_tests_properties(ggml_check PROPERTIES LABELS ggml)
if (GGML_CUDA)
    # The CUDA libraries (not GGML's) where N has fewer rows than their
    # tiles (K x N x M)
    add_test(
        NAME ggml_cuda_small_n
        COMMAND
            ggml-quant-bench --op mul_mat --acts f32,dot --providers cuda,!ggml-cuda --check
            --shapes 1024x2x7,1024x3x3,1024x4x2,1024x5x3,1024x6x5,1024x7x8,1024x6x17,1024x7x64
    )
    set_tests_properties(ggml_cuda_small_n PROPERTIES LABELS ggml_cuda)
    # The float gemms where M is less than a K split's tile (the tile's rows
    # outside M computed on the act's clamped rows, never stored)
    add_test(
        NAME ggml_cuda_partial_m_tiles
        COMMAND
            ggml-quant-bench --op mul_mat --types f16,bf16,f32 --acts f16,bf16,f32
            --providers cuda,!ggml-cuda --check
            --shapes 256x512x33,256x512x97,256x512x127,256x512x161,256x512x191,256x512x223
    )
    set_tests_properties(ggml_cuda_partial_m_tiles PROPERTIES LABELS ggml_cuda)
    # The integer gemms above M = 128, where M is not a whole number of their
    # blocks in M (the last shifted onto the one before)
    add_test(
        NAME ggml_cuda_int_big_m
        COMMAND
            ggml-quant-bench --op mul_mat --acts f32 --providers soa_mul_mat_cuda --check
            --shapes 256x512x129,256x512x200,256x512x225,256x512x257,256x512x300,256x512x321,256x512x350,256x512x401,256x512x481
    )
    set_tests_properties(ggml_cuda_int_big_m PROPERTIES LABELS ggml_cuda)
endif ()

# Properties of the schemes, checked by JIT (Halide's approximation testing).
add_executable(ggml_scheme_properties schemes/q8_1_properties.cpp)
target_include_directories(ggml_scheme_properties PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ggml_scheme_properties PRIVATE Halide::Halide Halide::Tools)
add_test(NAME ggml_scheme_properties COMMAND ggml_scheme_properties)
set_tests_properties(ggml_scheme_properties PROPERTIES LABELS ggml)
add_executable(ggml_legacy_properties schemes/legacy_properties.cpp)
target_include_directories(ggml_legacy_properties PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ggml_legacy_properties PRIVATE Halide::Halide Halide::Tools)
add_test(NAME ggml_legacy_properties COMMAND ggml_legacy_properties)
set_tests_properties(ggml_legacy_properties PROPERTIES LABELS ggml)
add_executable(ggml_kleidiai_properties schemes/kleidiai_properties.cpp)
target_include_directories(ggml_kleidiai_properties PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ggml_kleidiai_properties PRIVATE Halide::Halide Halide::Tools)
add_test(NAME ggml_kleidiai_properties COMMAND ggml_kleidiai_properties)
set_tests_properties(ggml_kleidiai_properties PROPERTIES LABELS ggml)
add_executable(ggml_mxfp4_properties schemes/mxfp4_properties.cpp)
target_include_directories(ggml_mxfp4_properties PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ggml_mxfp4_properties PRIVATE Halide::Halide Halide::Tools)
add_test(NAME ggml_mxfp4_properties COMMAND ggml_mxfp4_properties)
set_tests_properties(ggml_mxfp4_properties PROPERTIES LABELS ggml)
add_executable(ggml_kquants_properties schemes/kquants_properties.cpp)
target_include_directories(ggml_kquants_properties PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ggml_kquants_properties PRIVATE Halide::Halide Halide::Tools)
add_test(NAME ggml_kquants_properties COMMAND ggml_kquants_properties)
set_tests_properties(ggml_kquants_properties PROPERTIES LABELS ggml)
add_executable(ggml_iq_properties schemes/iq_properties.cpp)
target_include_directories(ggml_iq_properties PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ggml_iq_properties PRIVATE Halide::Halide Halide::Tools)
add_test(NAME ggml_iq_properties COMMAND ggml_iq_properties)
set_tests_properties(ggml_iq_properties PROPERTIES LABELS ggml)
# the grid formats' trusted encoders: GGML's quantizers, the JIT resolving
# the harness's extern (ggml_trusted_quantize) in the executable
add_executable(ggml_grid_properties schemes/grid_properties.cpp harness/trusted.cpp)
target_include_directories(ggml_grid_properties PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" harness)
target_link_libraries(ggml_grid_properties PRIVATE Halide::Halide Halide::Tools ggml::ggml)
set_target_properties(ggml_grid_properties PROPERTIES ENABLE_EXPORTS ON)
add_test(NAME ggml_grid_properties COMMAND ggml_grid_properties)
set_tests_properties(ggml_grid_properties PROPERTIES LABELS ggml)
add_executable(ggml_ternary_properties schemes/ternary_properties.cpp)
target_include_directories(ggml_ternary_properties PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ggml_ternary_properties PRIVATE Halide::Halide Halide::Tools)
add_test(NAME ggml_ternary_properties COMMAND ggml_ternary_properties)
set_tests_properties(ggml_ternary_properties PROPERTIES LABELS ggml)
add_executable(ggml_minifloat_properties schemes/minifloat_properties.cpp)
target_include_directories(ggml_minifloat_properties PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ggml_minifloat_properties PRIVATE Halide::Halide Halide::Tools)
add_test(NAME ggml_minifloat_properties COMMAND ggml_minifloat_properties)
set_tests_properties(ggml_minifloat_properties PROPERTIES LABELS ggml)

if (TARGET q4_0_q8_0_mul_mat_checked)
    add_executable(ggml_thread_pool_test runtime/thread_pool_test.cpp)
    target_link_libraries(ggml_thread_pool_test PRIVATE ggml_thread_pool q4_0_q8_0_mul_mat_checked)
    add_test(NAME ggml_thread_pool COMMAND ggml_thread_pool_test)
    set_tests_properties(ggml_thread_pool PROPERTIES LABELS ggml)
endif ()
