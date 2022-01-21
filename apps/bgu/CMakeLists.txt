cmake_minimum_required(VERSION 3.16)
project(bgu)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(bgu.generator SOURCES bgu_generator.cpp)

# Filters
add_halide_library(bgu FROM bgu.generator)
add_halide_library(bgu_auto_schedule FROM bgu.generator
                   GENERATOR bgu
                   AUTOSCHEDULER Halide::Mullapudi2016)

# Main executable
add_executable(bgu_filter filter.cpp)
target_link_libraries(bgu_filter
                      PRIVATE
                      Halide::ImageIO
                      bgu
                      bgu_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgb.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} rgb.png COPYONLY)
    add_test(NAME bgu_filter COMMAND bgu_filter rgb.png out.png)
    set_tests_properties(bgu_filter PROPERTIES
                         LABELS bgu
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
