cmake_minimum_required(VERSION 3.16)
project(local_laplacian)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(local_laplacian.generator
                     SOURCES local_laplacian_generator.cpp
                     LINK_LIBRARIES Halide::Tools)

# Filters
add_halide_library(local_laplacian FROM local_laplacian.generator)
add_halide_library(local_laplacian_auto_schedule FROM local_laplacian.generator
                   GENERATOR local_laplacian
                   AUTOSCHEDULER Halide::Mullapudi2016)

# Main executable
add_executable(local_laplacian_process process.cpp)
target_link_libraries(local_laplacian_process
                      PRIVATE
                      Halide::ImageIO
                      local_laplacian
                      local_laplacian_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgb.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} rgb.png COPYONLY)
    add_test(NAME local_laplacian_process COMMAND local_laplacian_process rgb.png 8 1 1 10 out.png)
    set_tests_properties(local_laplacian_process PROPERTIES
                         LABELS local_laplacian
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
