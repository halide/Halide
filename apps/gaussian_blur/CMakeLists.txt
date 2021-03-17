cmake_minimum_required(VERSION 3.16)
project(gaussian_blur)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_executable(gaussian_blur.generator gaussian_blur_generator.cpp)
target_link_libraries(gaussian_blur.generator PRIVATE Halide::Generator)

# Filters
add_halide_library(gaussian_blur FROM gaussian_blur.generator)
add_halide_library(gaussian_blur_auto_schedule FROM gaussian_blur.generator
                   GENERATOR gaussian_blur
                   AUTOSCHEDULER Halide::Mullapudi2016)

# Main executable
add_executable(gaussian_blur_filter filter.cpp)
target_link_libraries(gaussian_blur_filter PRIVATE
                      Halide::Tools
                      Halide::ImageIO
                      gaussian_blur
                      gaussian_blur_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgba.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} rgba.png COPYONLY)
    add_test(NAME gaussian_blur_filter
             COMMAND gaussian_blur_filter rgba.png out.png)
    set_tests_properties(gaussian_blur_filter PROPERTIES
                         LABELS gaussian_blur
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
