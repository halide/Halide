cmake_minimum_required(VERSION 3.28)
project(unsharp)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(unsharp.generator SOURCES unsharp_generator.cpp)

# Filters
add_halide_library(
    unsharp_pipeline
    FROM unsharp.generator
    GENERATOR unsharp
    FUNCTION_NAME unsharp
    FILE_BASE_NAME unsharp
)
add_halide_library(
    unsharp_auto_schedule
    FROM unsharp.generator
    GENERATOR unsharp
    AUTOSCHEDULER Halide::Mullapudi2016
)

# Main executable
set_target_properties(unsharp_pipeline PROPERTIES OUTPUT_NAME unsharp)
set_target_properties(unsharp_pipeline.runtime PROPERTIES OUTPUT_NAME unsharp.runtime)
add_executable(unsharp filter.cpp)
target_link_libraries(unsharp PRIVATE Halide::ImageIO unsharp_pipeline unsharp_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgba.png)
if (EXISTS ${IMAGE})
    add_test(NAME unsharp_filter COMMAND unsharp ${IMAGE} out.png)
    set_tests_properties(
        unsharp_filter
        PROPERTIES
        LABELS unsharp
        PASS_REGULAR_EXPRESSION "Success!"
        SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
    )
endif ()
