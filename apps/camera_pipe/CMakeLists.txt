cmake_minimum_required(VERSION 3.16)
project(camera_pipe)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_executable(camera_pipe.generator camera_pipe_generator.cpp)
target_link_libraries(camera_pipe.generator
                      PRIVATE
                      Halide::Generator
                      Halide::Tools)

# Filters
add_halide_library(camera_pipe FROM camera_pipe.generator)
add_halide_library(camera_pipe_auto_schedule FROM camera_pipe.generator
                   GENERATOR camera_pipe
                   PARAMS auto_schedule=true)

# Main executable
add_executable(camera_pipe_process process.cpp)
target_link_libraries(camera_pipe_process
                      PRIVATE
                      Halide::ImageIO
                      camera_pipe
                      camera_pipe_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgb.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} rgb.png COPYONLY)
    add_test(NAME camera_pipe_process
             COMMAND camera_pipe_process rgb.png 2 50 5 output.png)
endif ()
