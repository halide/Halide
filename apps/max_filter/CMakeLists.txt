cmake_minimum_required(VERSION 3.16)
project(max_filter)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_executable(max_filter.generator max_filter_generator.cpp)
target_link_libraries(max_filter.generator PRIVATE Halide::Generator Halide::Tools)

# Filters
add_halide_library(max_filter FROM max_filter.generator)
add_halide_library(max_filter_auto_schedule FROM max_filter.generator
                   GENERATOR max_filter
                   AUTOSCHEDULER Halide::Mullapudi2016)

# Main executable
add_executable(max_filter_filter filter.cpp)
target_link_libraries(max_filter_filter
                      PRIVATE
                      Halide::ImageIO
                      max_filter
                      max_filter_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgb.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} rgb.png COPYONLY)
    add_test(NAME max_filter_filter COMMAND max_filter_filter rgb.png out.png)
    set_tests_properties(max_filter_filter PROPERTIES
                         LABELS internal_app_tests
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
