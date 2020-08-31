cmake_minimum_required(VERSION 3.16)
project(interpolate)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_executable(interpolate.generator interpolate_generator.cpp)
target_link_libraries(interpolate.generator PRIVATE Halide::Generator Halide::Tools)

# Filters
add_halide_library(interpolate FROM interpolate.generator)
add_halide_library(interpolate_auto_schedule FROM interpolate.generator
                   GENERATOR interpolate
                   AUTOSCHEDULER Halide::Mullapudi2016
                   PARAMS auto_schedule=true)

# Main executable
add_executable(interpolate_filter filter.cpp)
target_link_libraries(interpolate_filter
                      PRIVATE
                      Halide::ImageIO
                      interpolate
                      interpolate_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgba.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} rgba.png COPYONLY)
    add_test(NAME interpolate_filter COMMAND interpolate_filter rgba.png out.png)
    set_tests_properties(interpolate_filter PROPERTIES
                         LABELS internal_app_tests
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
