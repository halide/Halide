cmake_minimum_required(VERSION 3.16)
project(stencil_chain)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Generator
add_halide_generator(stencil_chain.generator SOURCES stencil_chain_generator.cpp)

# Filters
add_halide_library(stencil_chain FROM stencil_chain.generator)
add_halide_library(stencil_chain_auto_schedule FROM stencil_chain.generator
                   GENERATOR stencil_chain
                   AUTOSCHEDULER Halide::Mullapudi2016)

# Main executable
add_executable(stencil_chain_process process.cpp)
target_link_libraries(stencil_chain_process
                      PRIVATE
                      Halide::ImageIO
                      stencil_chain
                      stencil_chain_auto_schedule)

# Test that the app actually works!
set(IMAGE ${CMAKE_CURRENT_LIST_DIR}/../images/rgb.png)
if (EXISTS ${IMAGE})
    configure_file(${IMAGE} rgb.png COPYONLY)
    add_test(NAME stencil_chain_process COMMAND stencil_chain_process rgb.png 10 out.png)
    set_tests_properties(stencil_chain_process PROPERTIES
                         LABELS stencil_chain
                         PASS_REGULAR_EXPRESSION "Success!"
                         SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
endif ()
