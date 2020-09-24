cmake_minimum_required(VERSION 3.16)
project(conv_layer)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_executable(conv_layer.generator conv_layer_generator.cpp)
target_link_libraries(conv_layer.generator
                      PRIVATE
                      Halide::Generator)

# Filters
add_halide_library(conv_layer FROM conv_layer.generator)
add_halide_library(conv_layer_auto_schedule FROM conv_layer.generator
                   GENERATOR conv_layer
                   AUTOSCHEDULER Halide::Mullapudi2016)

# Main executable
add_executable(conv_layer_process process.cpp)
target_link_libraries(conv_layer_process
                      PRIVATE
                      Halide::ImageIO
                      conv_layer
                      conv_layer_auto_schedule)

# Test that the app actually works!
add_test(NAME conv_layer_process COMMAND conv_layer_process)
set_tests_properties(conv_layer_process PROPERTIES
                     LABELS internal_app_tests
                     PASS_REGULAR_EXPRESSION "Success!"
                     SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
