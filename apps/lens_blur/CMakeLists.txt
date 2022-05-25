cmake_minimum_required(VERSION 3.16)
project(lens_blur)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(lens_blur.generator SOURCES lens_blur_generator.cpp)

# Filters
add_halide_library(lens_blur FROM lens_blur.generator)
add_halide_library(lens_blur_auto_schedule FROM lens_blur.generator
                   GENERATOR lens_blur
                   AUTOSCHEDULER Halide::Mullapudi2016)

# Main executable
add_executable(lens_blur_filter process.cpp)
target_link_libraries(lens_blur_filter
                      PRIVATE
                      Halide::ImageIO
                      lens_blur
                      lens_blur_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgb_small.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} rgb_small.png COPYONLY)
    add_test(NAME lens_blur_filter
             COMMAND lens_blur_filter rgb_small.png 32 13 0.5 32 3 out.png)
    set_tests_properties(lens_blur_filter PROPERTIES
                         LABELS lens_blur
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
