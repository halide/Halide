cmake_minimum_required(VERSION 3.28)
project(harris)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(harris.generator SOURCES harris_generator.cpp)

# Filters
add_halide_library(
    harris_pipeline
    FROM harris.generator
    GENERATOR harris
    FUNCTION_NAME harris
    FILE_BASE_NAME harris
)
add_halide_library(
    harris_auto_schedule
    FROM harris.generator
    GENERATOR harris
    AUTOSCHEDULER Halide::Mullapudi2016
    PARAMS autoscheduler.experimental_gpu_schedule=1
)

# Main executable
set_target_properties(harris_pipeline PROPERTIES OUTPUT_NAME harris)
set_target_properties(harris_pipeline.runtime PROPERTIES OUTPUT_NAME harris.runtime)
add_executable(harris filter.cpp)
target_link_libraries(harris PRIVATE Halide::ImageIO harris_pipeline harris_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgba.png)
if (EXISTS ${IMAGE})
    add_test(NAME harris_filter COMMAND harris ${IMAGE} out.png)
    set_tests_properties(
        harris_filter
        PROPERTIES
        LABELS harris
        PASS_REGULAR_EXPRESSION "Success!"
        SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
    )
endif ()
