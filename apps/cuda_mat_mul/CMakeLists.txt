cmake_minimum_required(VERSION 3.28)
project(cuda_mat_mul)

# This just checks whether CUDA is available ahead of time to allow
# skipping this app when CUDA/cuBLAS are not installed on the system.
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
add_halide_generator(mat_mul.generator SOURCES mat_mul_generator.cpp)

# Filters
add_halide_library(mat_mul FROM mat_mul.generator
                   TARGETS host
                   FEATURES cuda cuda_capability_50
                   PARAMS size=1024)

# Main executable
add_executable(runner runner.cpp)
target_link_libraries(runner PRIVATE mat_mul Halide::Tools CUDA::cudart CUDA::cublas)

# Test that the app actually works!
add_test(NAME mat_mul COMMAND runner)
set_tests_properties(mat_mul PROPERTIES
                     LABELS cuda_mat_mul
                     PASS_REGULAR_EXPRESSION "Success!"
                     SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
