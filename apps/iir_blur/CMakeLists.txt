cmake_minimum_required(VERSION 3.28)
project(iir_blur)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(iir_blur.generator SOURCES iir_blur_generator.cpp)

# Filters
add_halide_library(
    iir_blur_pipeline
    FROM iir_blur.generator
    GENERATOR iir_blur
    FUNCTION_NAME iir_blur
    FILE_BASE_NAME iir_blur
)
add_halide_library(
    iir_blur_auto_schedule
    FROM iir_blur.generator
    GENERATOR iir_blur
    AUTOSCHEDULER Halide::Mullapudi2016
    PARAMS autoscheduler.experimental_gpu_schedule=1
)

# Main executable
set_target_properties(iir_blur_pipeline PROPERTIES OUTPUT_NAME iir_blur)
set_target_properties(iir_blur_pipeline.runtime PROPERTIES OUTPUT_NAME iir_blur.runtime)
add_executable(iir_blur filter.cpp)
target_link_libraries(
    iir_blur
    PRIVATE
    Halide::Tools
    Halide::ImageIO
    iir_blur_pipeline
    iir_blur_auto_schedule
)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgb.png)
if (EXISTS ${IMAGE})
    if (Halide_TARGET MATCHES "opencl")
        # Error message:
        #
        # Error: OpenCL error: CL_INVALID_COMMAND_QUEUE clFinish failed
        message(WARNING "Skipping Mullapudi2016's GPU auto-schedules for OpenCL target.")
    else ()
        add_test(NAME iir_blur_filter COMMAND iir_blur ${IMAGE} out.png)
        set_tests_properties(
            iir_blur_filter
            PROPERTIES
            LABELS iir_blur
            PASS_REGULAR_EXPRESSION "Success!"
            SKIP_REGULAR_EXPRESSION "\\[SKIP\\]"
        )
    endif ()
endif ()
