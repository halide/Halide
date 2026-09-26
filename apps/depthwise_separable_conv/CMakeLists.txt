cmake_minimum_required(VERSION 3.28)
project(depthwise_separable_conv)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(
    depthwise_separable_conv.generator
    SOURCES depthwise_separable_conv_generator.cpp
)

# Filters
add_halide_library(
    depthwise_separable_conv_pipeline
    FROM depthwise_separable_conv.generator
    GENERATOR depthwise_separable_conv
    FUNCTION_NAME depthwise_separable_conv
    FILE_BASE_NAME depthwise_separable_conv
)
add_halide_library(
    depthwise_separable_conv_auto_schedule
    FROM depthwise_separable_conv.generator
    GENERATOR depthwise_separable_conv
    AUTOSCHEDULER Halide::Mullapudi2016
    PARAMS autoscheduler.experimental_gpu_schedule=1
)

# Main executable
set_target_properties(depthwise_separable_conv_pipeline
    PROPERTIES
    OUTPUT_NAME depthwise_separable_conv
)
set_target_properties(depthwise_separable_conv_pipeline.runtime
    PROPERTIES
    OUTPUT_NAME depthwise_separable_conv.runtime
)
add_executable(depthwise_separable_conv process.cpp)
target_link_libraries(
    depthwise_separable_conv
    PRIVATE
    Halide::ImageIO
    depthwise_separable_conv_pipeline
    depthwise_separable_conv_auto_schedule
)

# Test that the app actually works!
add_test(NAME depthwise_separable_conv_process COMMAND depthwise_separable_conv)
set_tests_properties(
    depthwise_separable_conv_process
    PROPERTIES
    LABELS depthwise_separable_conv
    PASS_REGULAR_EXPRESSION "Success!"
    SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
)
