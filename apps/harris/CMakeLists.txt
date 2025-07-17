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

set(_harris_autoscheduler_params autoscheduler.experimental_gpu_schedule=1)

if(Halide_TARGET MATCHES "opencl|metal")
    # Set last_level_cache per GPU block to an extremely small value. This
    # eliminates all `.compute_at` in the generated schedules, which in turn
    # eliminates all GPU shared memory allocations.
    list(APPEND _harris_autoscheduler_params
        autoscheduler.last_level_cache_size=1000
    )
endif()

# Filters
add_halide_library(harris FROM harris.generator)
add_halide_library(harris_auto_schedule FROM harris.generator
                   GENERATOR harris
                   AUTOSCHEDULER Halide::Mullapudi2016
                   PARAMS ${_harris_autoscheduler_params})

# Main executable
add_executable(harris_filter filter.cpp)
target_link_libraries(harris_filter
                      PRIVATE
                      Halide::ImageIO
                      harris
                      harris_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgba.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} rgba.png COPYONLY)
    add_test(NAME harris_filter
             COMMAND harris_filter rgba.png out.png)
    set_tests_properties(harris_filter PROPERTIES
                         LABELS harris
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
