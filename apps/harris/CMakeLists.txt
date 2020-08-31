cmake_minimum_required(VERSION 3.16)
project(harris)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_executable(harris.generator harris_generator.cpp)
target_link_libraries(harris.generator PRIVATE Halide::Generator Halide::Tools)

# Filters
add_halide_library(harris FROM harris.generator)
add_halide_library(harris_auto_schedule FROM harris.generator
                   GENERATOR harris
                   AUTOSCHEDULER Halide::Mullapudi2016)

# Main executable
add_executable(harris_filter filter.cpp)
target_link_libraries(harris_filter
                      PRIVATE
                      Halide::ImageIO
                      harris
                      harris_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgb.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} rgb.png COPYONLY)
    add_test(NAME harris_filter
             COMMAND harris_filter rgb.png out.png)
    set_tests_properties(harris_filter PROPERTIES
                         LABELS internal_app_tests
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
