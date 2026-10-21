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
)
target_include_directories(ggml-quant-bench PRIVATE harness "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(ggml-quant-bench PRIVATE ggml::ggml ggml_thread_pool ${GGML_HALIDE_LIBRARIES})
if (GGML_CPU_KLEIDIAI)
    # KleidiAI's packers, the reference of the qsi8cx and qai8dx codecs
    target_compile_definitions(ggml-quant-bench PRIVATE GQ_KLEIDIAI)
endif ()

add_test(NAME ggml_check COMMAND ggml-quant-bench --check --shapes smoke)
set_tests_properties(ggml_check PROPERTIES LABELS ggml)

# Properties of the schemes, checked by JIT (Halide's approximation testing).
add_executable(ggml_scheme_properties schemes/q8_1_properties.cpp)
target_include_directories(ggml_scheme_properties PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ggml_scheme_properties PRIVATE Halide::Halide Halide::Tools)
add_test(NAME ggml_scheme_properties COMMAND ggml_scheme_properties)
set_tests_properties(ggml_scheme_properties PROPERTIES LABELS ggml)
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
