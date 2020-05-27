cmake_minimum_required(VERSION 3.16)
project(cuda_mat_mul)

# This just checks whether CUDA is available ahead of time to allow CTest to
# skip this app when CUDA/cuBLAS are not installed on the system.
find_package(CUDA)
if (NOT CUDA_FOUND)
    message(FATAL_ERROR "[SKIP] Could NOT find CUDA")
endif ()

if (NOT CUDA_CUBLAS_LIBRARIES)
    message(FATAL_ERROR "[SKIP] Could NOT find cuBLAS")
endif ()

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_executable(mat_mul.generator mat_mul_generator.cpp)
target_link_libraries(mat_mul.generator PRIVATE Halide::Generator)

# Filters
add_halide_library(mat_mul FROM mat_mul.generator
                   TARGETS host
                   FEATURES cuda cuda_capability_50
                   PARAMS size=1024)

# Main executable
add_executable(runner runner.cpp)
target_include_directories(runner PRIVATE ${CUDA_INCLUDE_DIRS})
target_link_libraries(runner PRIVATE Halide::Tools mat_mul ${CUDA_LIBRARIES} ${CUDA_CUBLAS_LIBRARIES})

# Test that the app actually works!
add_test(NAME mat_mul COMMAND runner)
