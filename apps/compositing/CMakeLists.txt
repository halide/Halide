cmake_minimum_required(VERSION 3.22)
project(compositing)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(compositing.generator
                     SOURCES compositing_generator.cpp
                     LINK_LIBRARIES Halide::Tools)

# Filters
add_halide_library(compositing FROM compositing.generator)
add_halide_library(compositing_auto_schedule FROM compositing.generator
                   GENERATOR compositing
                   AUTOSCHEDULER Halide::Mullapudi2016)

# Main executable
add_executable(compositing_process process.cpp)
target_link_libraries(compositing_process
                      PRIVATE
                      Halide::ImageIO
                      compositing
                      compositing_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgba.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} rgba.png COPYONLY)
    add_test(NAME compositing_process COMMAND compositing_process rgba.png 10 out.png)
    set_tests_properties(compositing_process PROPERTIES
                         LABELS compositing
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
