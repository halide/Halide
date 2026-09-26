cmake_minimum_required(VERSION 3.28)
project(compositing)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(
    compositing.generator
    SOURCES compositing_generator.cpp
    LINK_LIBRARIES Halide::Tools
)

# Filters
add_halide_library(
    compositing_pipeline
    FROM compositing.generator
    GENERATOR compositing
    FUNCTION_NAME compositing
    FILE_BASE_NAME compositing
)
add_halide_library(
    compositing_auto_schedule
    FROM compositing.generator
    GENERATOR compositing
    AUTOSCHEDULER Halide::Mullapudi2016
    PARAMS autoscheduler.experimental_gpu_schedule=1
)

# Main executable
set_target_properties(compositing_pipeline PROPERTIES OUTPUT_NAME compositing)
set_target_properties(compositing_pipeline.runtime PROPERTIES OUTPUT_NAME compositing.runtime)
add_executable(compositing process.cpp)
target_link_libraries(
    compositing
    PRIVATE
    Halide::ImageIO
    compositing_pipeline
    compositing_auto_schedule
)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgba.png)
if (EXISTS ${IMAGE})
    add_test(NAME compositing_process COMMAND compositing ${IMAGE} 10 out.png)
    set_tests_properties(
        compositing_process
        PROPERTIES
        LABELS compositing
        PASS_REGULAR_EXPRESSION "Success!"
        SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
    )
endif ()
