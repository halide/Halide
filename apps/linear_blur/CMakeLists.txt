cmake_minimum_required(VERSION 3.28)
project(linear_blur)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Component generators (standalone executables that also produce stubs)
add_halide_generator(srgb_to_linear.generator SOURCES srgb_to_linear_generator.cpp)
add_halide_generator(simple_blur.generator SOURCES simple_blur_generator.cpp)
add_halide_generator(linear_to_srgb.generator SOURCES linear_to_srgb_generator.cpp)

# Generate stub headers for pipeline composition
add_halide_stub(srgb_to_linear.stub FROM srgb_to_linear.generator GENERATOR srgb_to_linear)
add_halide_stub(simple_blur.stub FROM simple_blur.generator GENERATOR simple_blur)
add_halide_stub(linear_to_srgb.stub FROM linear_to_srgb.generator GENERATOR linear_to_srgb)

# Composite generator: includes all component sources (for HALIDE_REGISTER_GENERATOR
# factory functions) and links stub headers (for compile-time composition).
add_halide_generator(
    linear_blur.generator
    SOURCES
    linear_blur_generator.cpp
    srgb_to_linear_generator.cpp
    simple_blur_generator.cpp
    linear_to_srgb_generator.cpp

    LINK_LIBRARIES
    srgb_to_linear.stub
    simple_blur.stub
    linear_to_srgb.stub
)

# AOT-compiled filter libraries
add_halide_library(simple_blur FROM simple_blur.generator
                   PARAMS
                   input.type=float32 input.dim=3
                   output.type=float32 output.dim=3)
add_halide_library(linear_blur FROM linear_blur.generator
                   AUTOSCHEDULER Halide::Mullapudi2016)

# Main executable
add_executable(linear_blur_process run_linear_blur.cpp)
target_link_libraries(linear_blur_process
                      PRIVATE
                      Halide::ImageIO
                      linear_blur
                      simple_blur)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgb.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} rgb.png COPYONLY)
    add_test(NAME linear_blur_test
             COMMAND linear_blur_process 1 rgb.png out.png)
    set_tests_properties(linear_blur_test PROPERTIES
                         LABELS linear_blur
                         PASS_REGULAR_EXPRESSION "Using linear blur"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
