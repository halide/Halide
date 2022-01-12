cmake_minimum_required(VERSION 3.16)
project(camera_pipe)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(camera_pipe.generator
                     SOURCES camera_pipe_generator.cpp
                     LINK_LIBRARIES Halide::Tools)

# Filters
add_halide_library(camera_pipe FROM camera_pipe.generator)
add_halide_library(camera_pipe_auto_schedule FROM camera_pipe.generator
                   GENERATOR camera_pipe
                   AUTOSCHEDULER Halide::Mullapudi2016)

# Main executable
add_executable(camera_pipe_process process.cpp)
target_link_libraries(camera_pipe_process
                      PRIVATE
                      Halide::ImageIO
                      camera_pipe
                      camera_pipe_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/bayer_raw.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} bayer_raw.png COPYONLY)
    add_test(NAME camera_pipe_process
             COMMAND camera_pipe_process bayer_raw.png 3700 2.0 50 1.0 5 out.png)
    set_tests_properties(camera_pipe_process PROPERTIES
                         LABELS camera_pipe
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
