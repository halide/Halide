cmake_minimum_required(VERSION 3.28)
project(hexagon_benchmarks)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Add Halide libraries
add_halide_generator(dilate3x3.generator SOURCES dilate3x3_generator.cpp)
add_halide_library(dilate3x3 FROM dilate3x3.generator)

add_halide_generator(gaussian5x5.generator SOURCES gaussian5x5_generator.cpp)
add_halide_library(gaussian5x5 FROM gaussian5x5.generator)

add_halide_generator(median3x3.generator SOURCES median3x3_generator.cpp)
add_halide_library(median3x3 FROM median3x3.generator)

add_halide_generator(sobel.generator SOURCES sobel_generator.cpp)
add_halide_library(sobel FROM sobel.generator)

# Main executable
add_executable(process process.cpp)
target_compile_options(process PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O2>)
target_compile_definitions(process PRIVATE DILATE3X3 GAUSSIAN5X5 MEDIAN3X3 SOBEL)
target_link_libraries(process PRIVATE Halide::Tools dilate3x3 gaussian5x5 median3x3 sobel)

if (Halide_TARGET MATCHES "hvx")
    target_compile_definitions(process PRIVATE TARGET_HAS_HVX)
endif ()

# Test that the app actually works!
add_test(NAME hexagon_benchmarks COMMAND process -n 1)
set_tests_properties(hexagon_benchmarks PROPERTIES
                     LABELS hexagon_benchmarks
                     PASS_REGULAR_EXPRESSION "Success!"
                     SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")