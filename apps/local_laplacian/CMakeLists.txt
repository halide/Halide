cmake_minimum_required(VERSION 3.28)
project(local_laplacian)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(local_laplacian.generator
                     SOURCES local_laplacian_generator.cpp
                     LINK_LIBRARIES Halide::Tools)

# Filters
add_halide_library(local_laplacian FROM local_laplacian.generator)
add_halide_library(local_laplacian_auto_schedule FROM local_laplacian.generator
                   GENERATOR local_laplacian
                   AUTOSCHEDULER Halide::Mullapudi2016
                   # When target=host-cuda or host-metal, limit the GPU shared
                   # memory per block to avoid gpu kernel launch failure.
                   PARAMS autoscheduler.last_level_cache_size=30000 autoscheduler.parallelism=4096 autoscheduler.experimental_gpu_schedule=1
                   )

# Main executable
add_executable(local_laplacian_process process.cpp)
target_link_libraries(local_laplacian_process
                      PRIVATE
                      Halide::ImageIO
                      local_laplacian
                      local_laplacian_auto_schedule)

# Visualization
add_halide_library(local_laplacian_viz FROM local_laplacian.generator
                   GENERATOR local_laplacian
                   FUNCTION_NAME local_laplacian
                   FEATURES trace_all)

add_executable(local_laplacian_process_viz process.cpp)
target_link_libraries(local_laplacian_process_viz
                      PRIVATE
                      Halide::ImageIO
                      local_laplacian_viz)
target_compile_definitions(local_laplacian_process_viz PRIVATE NO_AUTO_SCHEDULE)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgb.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} rgb.png COPYONLY)
    add_test(NAME local_laplacian_process COMMAND local_laplacian_process rgb.png 8 1 1 10 out.png)
    set_tests_properties(local_laplacian_process PROPERTIES
                         LABELS local_laplacian
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
