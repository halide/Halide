cmake_minimum_required(VERSION 3.28)
project(interpolate)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(interpolate.generator SOURCES interpolate_generator.cpp)

# Filters
add_halide_library(
    interpolate_pipeline
    FROM interpolate.generator
    GENERATOR interpolate
    FUNCTION_NAME interpolate
    FILE_BASE_NAME interpolate
)
add_halide_library(
    interpolate_auto_schedule
    FROM interpolate.generator
    GENERATOR interpolate
    AUTOSCHEDULER Halide::Mullapudi2016
)

# Main executable
set_target_properties(interpolate_pipeline PROPERTIES OUTPUT_NAME interpolate)
set_target_properties(interpolate_pipeline.runtime PROPERTIES OUTPUT_NAME interpolate.runtime)
add_executable(interpolate filter.cpp)
target_link_libraries(
    interpolate
    PRIVATE
    Halide::ImageIO
    interpolate_pipeline
    interpolate_auto_schedule
)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgba.png)
if (EXISTS ${IMAGE})
    add_test(NAME interpolate_filter COMMAND interpolate ${IMAGE} out.png)
    set_tests_properties(
        interpolate_filter
        PROPERTIES
        LABELS interpolate
        PASS_REGULAR_EXPRESSION "Success!"
        SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
    )
endif ()
