cmake_minimum_required(VERSION 3.16)
project(cuda_mat_mul)

# This just checks whether CUDA is available ahead of time to allow
# skipping this app when CUDA/cuBLAS are not installed on the system.
find_package(CUDA)
if (NOT CUDA_FOUND)
    message(WARNING "Could NOT find CUDA")
    return()
endif ()

if (NOT CUDA_CUBLAS_LIBRARIES)
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
add_halide_library(mat_mul_50 FROM mat_mul.generator
                   TARGETS host
                   FEATURES cuda cuda_capability_50
                   PARAMS size=2048)
add_halide_library(mat_mul_70 FROM mat_mul.generator
                   TARGETS host
                   FEATURES cuda cuda_capability_70
                   PARAMS size=2048)

# Main executable
add_executable(runner runner.cpp)
target_include_directories(runner PRIVATE ${CUDA_INCLUDE_DIRS})
target_link_libraries(runner PRIVATE Halide::Halide Halide::Tools mat_mul_50 mat_mul_70 ${CUDA_LIBRARIES} ${CUDA_CUBLAS_LIBRARIES})

# Test that the app actually works!
add_test(NAME mat_mul COMMAND runner)
set_tests_properties(mat_mul PROPERTIES
                     LABELS cuda_mat_mul
                     PASS_REGULAR_EXPRESSION "Success!"
                     SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
