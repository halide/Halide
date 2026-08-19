cmake_minimum_required(VERSION 3.28)
project(iir_cascade)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(
    iir_cascade.generator
    SOURCES iir_cascade_generator.cpp
    LINK_LIBRARIES Halide::Tools
)

# Filters
add_halide_library(
    iir_cascade
    FROM iir_cascade.generator
    ${IIR_CASCADE_FEATURES}
    PARAMS inductive=true
)
add_halide_library(
    iir_cascade_noninductive
    FROM iir_cascade.generator
    GENERATOR iir_cascade
    ${IIR_CASCADE_FEATURES}
    PARAMS inductive=false
)

# Main executable
add_executable(iir_cascade_test test.cpp)
target_link_libraries(iir_cascade_test PRIVATE Halide::Tools iir_cascade iir_cascade_noninductive)

# Test that the app actually works!
add_test(NAME iir_cascade_test COMMAND iir_cascade_test)
set_tests_properties(
    iir_cascade_test
    PROPERTIES
    LABELS iir_cascade
    PASS_REGULAR_EXPRESSION "Success!"
    SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
)
