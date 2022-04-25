cmake_minimum_required(VERSION 3.16)
project(nl_means)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(nl_means.generator SOURCES nl_means_generator.cpp)

# Filters
add_halide_library(nl_means FROM nl_means.generator)
add_halide_library(nl_means_auto_schedule FROM nl_means.generator
                   GENERATOR nl_means
                   AUTOSCHEDULER Halide::Mullapudi2016)

# Main executable
add_executable(nl_means_process process.cpp)
target_link_libraries(nl_means_process
                      PRIVATE
                      Halide::ImageIO
                      nl_means
                      nl_means_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgb.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} rgb.png COPYONLY)
    add_test(NAME nl_means_process COMMAND nl_means_process rgb.png 7 7 0.12 10 out.png)
    set_tests_properties(nl_means_process PROPERTIES
                         LABELS nl_means
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
