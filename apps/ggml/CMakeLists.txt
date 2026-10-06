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

include(formats.cmake)

add_executable(
    ggml-quant-bench
    harness/bench.cpp
    harness/codec.cpp
    harness/ggml_providers.cpp
    harness/halide_providers.cpp
    harness/main.cpp
    harness/oracle.cpp
    harness/shapes.cpp
)
target_include_directories(ggml-quant-bench PRIVATE harness "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(ggml-quant-bench PRIVATE ggml::ggml ${GGML_HALIDE_LIBRARIES})

add_test(NAME ggml_check COMMAND ggml-quant-bench --check --shapes smoke)
set_tests_properties(ggml_check PROPERTIES LABELS ggml)
