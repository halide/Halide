cmake_minimum_required(VERSION 3.16)
project(unsharp)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_executable(unsharp.generator unsharp_generator.cpp)
target_link_libraries(unsharp.generator PRIVATE Halide::Generator Halide::Tools)

# Filters
add_halide_library(unsharp FROM unsharp.generator)
add_halide_library(unsharp_auto_schedule FROM unsharp.generator
                   GENERATOR unsharp
                   AUTOSCHEDULER Halide::Mullapudi2016)

# Main executable
add_executable(unsharp_filter filter.cpp)
target_link_libraries(unsharp_filter
                      PRIVATE
                      Halide::ImageIO
                      unsharp
                      unsharp_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgb.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} rgb.png COPYONLY)
    add_test(NAME unsharp_filter COMMAND unsharp_filter rgb.png out.png)
    set_tests_properties(unsharp_filter PROPERTIES
                         LABELS unsharp
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
