cmake_minimum_required(VERSION 3.16)
project(hist)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(hist.generator SOURCES hist_generator.cpp)

# Filters
add_halide_library(hist FROM hist.generator)
add_halide_library(hist_auto_schedule FROM hist.generator
                   GENERATOR hist
                   AUTOSCHEDULER Halide::Mullapudi2016)

# Main executable
add_executable(hist_filter filter.cpp)
target_link_libraries(hist_filter
                      PRIVATE
                      Halide::ImageIO
                      Halide::Tools
                      hist
                      hist_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgba.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} rgba.png COPYONLY)
    add_test(NAME hist_filter
             COMMAND hist_filter rgba.png out.png)
    set_tests_properties(hist_filter PROPERTIES
                         LABELS hist
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
