cmake_minimum_required(VERSION 3.16...3.20)
project(hexagon-profiling-dsp)

set(HEXAGON_SDK_ROOT $ENV{HEXAGON_SDK_ROOT})

add_library(hexagon-profiling SHARED
    profiling.cpp)
target_compile_features(hexagon-profiling PRIVATE
    cxx_std_17)

# we need halide struct definitions
# but can't find_package(Halide) for 32-bit compiler (only 64-bit available)
# therefore can't link a halide hexagon runtime target using target_link_libraries
target_include_directories(hexagon-profiling PRIVATE
    ../../../src/runtime/)
