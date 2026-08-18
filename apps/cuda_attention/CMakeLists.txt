cmake_minimum_required(VERSION 3.28)
project(cuda_attention)

# This schedule is for CUDA's tensor cores, and the throughput reference it
# prints is cuBLAS, so there is nothing here to build for any other device API.
if (NOT Halide_TARGET MATCHES "cuda")
    message(STATUS "Skipping cuda_attention: Halide_TARGET is not a CUDA target")
    return()
endif ()

# This just checks whether CUDA is available ahead of time to allow skipping
# this app when CUDA/cuBLAS are not installed on the system.
find_package(CUDAToolkit)
if (NOT CUDAToolkit_FOUND)
    message(WARNING "Could NOT find CUDA")
    return()
endif ()

if (NOT TARGET CUDA::cublas)
    message(WARNING "Could NOT find cuBLAS")
    return()
endif ()

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(attention.generator SOURCES attention_generator.cpp)

# The shape is compile time, because the schedule is built around it. It has to
# match the constants in runner.cpp.
set(QUERIES 16384)
set(KEYS 64)
set(DEPTH 64)
set(OUT_DEPTH 64)
set(SHAPE queries=${QUERIES} keys=${KEYS} depth=${DEPTH} out_depth=${OUT_DEPTH})

# Filters. The tensor cores need compute capability 7.0 or above.
add_halide_library(
    attention
    FROM attention.generator
    FEATURES cuda cuda_capability_80
    PARAMS ${SHAPE}
)

# Main executable
add_executable(runner runner.cpp)
target_compile_definitions(
    runner
    PRIVATE
    QUERIES=${QUERIES} KEYS=${KEYS} DEPTH=${DEPTH} OUT_DEPTH=${OUT_DEPTH}
)
target_link_libraries(
    runner
    PRIVATE
    attention Halide::Halide Halide::Tools
    CUDA::cudart CUDA::cublas
)

# Test that the app actually works!
add_test(NAME cuda_attention COMMAND runner)
set_tests_properties(
    cuda_attention
    PROPERTIES
    LABELS cuda_attention
    PASS_REGULAR_EXPRESSION "Success!"
    SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
)
