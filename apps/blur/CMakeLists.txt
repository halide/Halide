cmake_minimum_required(VERSION 3.16)
project(blur)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)
find_package(OpenMP)

# Generator
add_halide_generator(blur.generator SOURCES halide_blur_generator.cpp)

# Filters
add_halide_library(halide_blur FROM blur.generator)

# Main executable
add_executable(blur_test test.cpp)
target_compile_options(blur_test PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O2>)
target_link_libraries(blur_test
                      PRIVATE
                      Halide::Tools
                      halide_blur
                      $<TARGET_NAME_IF_EXISTS:OpenMP::OpenMP_CXX>)

# Test that the app actually works!
add_test(NAME blur_app COMMAND blur_test)
set_tests_properties(blur_app PROPERTIES
                     LABELS blur
                     PASS_REGULAR_EXPRESSION "Success!"
                     SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
