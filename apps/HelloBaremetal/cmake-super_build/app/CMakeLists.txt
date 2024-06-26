cmake_minimum_required(VERSION 3.22)

# Enable assembly language (.s) support additionally
project(HelloBaremetal-app LANGUAGES C CXX ASM)

if (NOT DEFINED GEN_TARGET)
    message(FATAL_ERROR "CMakeVariable GEN_TARGET is not set")
endif()

# Set up C++ language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Import Halide. Instead of the package Halide, HalideHelpers is enough as we don't use JIT mode.
find_package(HalideHelpers REQUIRED)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# Import pre-built Generator as package, which lets TARGET add.generator available
find_package(HelloBaremetal-add_generator REQUIRED)

# Generate filter function
add_halide_library(add
                   FROM ${GEN_TARGET}
                   GENERATOR add
                   # You may set Target feature in addition to what is set in the toolchain file
                   FEATURES debug

                   # In baremetal, set parallelism=1 because thread support is unavailable.
                   AUTOSCHEDULER Halide::Mullapudi2016
                   PARAMS autoscheduler.parallelism=1)

# Main executable
add_executable(add_filter ${SRC_DIR}/filter.cpp)
target_link_libraries(add_filter PRIVATE
                      Halide::ImageIO
                      add)

# Depending on the target CPU, we need to enable Arm NEON manually
# if the executable contains any SIMD instructions.
target_sources(add_filter PRIVATE "$<$<BOOL:${BAREMETAL}>:${SRC_DIR}/enable_neon.s>")
# It is safe to do it in boot up process before main function (or C runtime) starts.
target_link_options(add_filter PRIVATE "$<$<BOOL:${BAREMETAL}>:--entry=_enable_neon>")
