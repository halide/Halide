cmake_minimum_required(VERSION 3.28)
project(HelloBaremetal)

# Set up C++ language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

find_package(Halide REQUIRED)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Setup Generator
add_halide_generator(add.generator SOURCES ${SRC_DIR}/add_generator.cpp)

# Generate filter function
add_halide_library(
    add
    FROM add.generator
    GENERATOR add
    # You may set Target feature in addition to what is set in the toolchain file
    FEATURES debug
    # In baremetal, set parallelism=1 because thread support is unavailable.
    AUTOSCHEDULER Halide::Mullapudi2016
    PARAMS autoscheduler.parallelism=1
)

# Main executable
add_executable(add_filter ${SRC_DIR}/filter.cpp)
target_link_libraries(add_filter PRIVATE Halide::ImageIO add)

# Test that the app actually works, under QEMU with semihosting. Only
# meaningful for the actual bare-metal cross-compile; in the host-target
# build mode there's no toolchain to run it through.
if (CMAKE_CROSSCOMPILING)
    enable_testing()
    set(IMAGE ${SRC_DIR}/../images/gray_small.pgm)
    if (EXISTS ${IMAGE})
        add_test(
            NAME add_filter
            COMMAND ${SRC_DIR}/run_baremetal.sh $<TARGET_FILE:add_filter> ${IMAGE} 16 out.pgm
        )
        set_tests_properties(add_filter PROPERTIES PASS_REGULAR_EXPRESSION "Success!")
    endif ()
endif ()
