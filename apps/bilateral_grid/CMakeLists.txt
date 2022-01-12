cmake_minimum_required(VERSION 3.16)
project(bilateral_grid)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(bilateral_grid.generator
                     SOURCES bilateral_grid_generator.cpp
                     LINK_LIBRARIES Halide::Tools)

# Filters
add_halide_library(bilateral_grid FROM bilateral_grid.generator
                   STMT bilateral_grid_STMT
                   SCHEDULE bilateral_grid_SCHEDULE)

add_halide_library(bilateral_grid_auto_schedule FROM bilateral_grid.generator
                   GENERATOR bilateral_grid
                   STMT bilateral_grid_auto_schedule_STMT
                   SCHEDULE bilateral_grid_auto_schedule_SCHEDULE
                   AUTOSCHEDULER Halide::Mullapudi2016)

# Main executable
add_executable(bilateral_grid_process filter.cpp)
target_link_libraries(bilateral_grid_process
                      PRIVATE
                      Halide::ImageIO # For halide_image_io.h
                      Halide::Tools # For halide_benchmark.h
                      bilateral_grid
                      bilateral_grid_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/gray.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} gray.png COPYONLY)
    add_test(NAME bilateral_grid_process
             COMMAND bilateral_grid_process gray.png out.png 0.1 10)
    set_tests_properties(bilateral_grid_process PROPERTIES
                         LABELS bilateral_grid
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
