cmake_minimum_required(VERSION 3.28)
project(HelloAndroidCamera2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# This file configures itself twice: once as a host build (to compile the
# generators) and once as this target build (driven by AGP's
# externalNativeBuild, under the NDK cross toolchain). See "Use
# add_halide_generator" in doc/HalideCMakePackage.md.
set(HelloAndroidCamera2-halide_generators_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../.cxx-generators"
)
if (ANDROID)
    # NO_CMAKE_FIND_ROOT_PATH: bypass the NDK toolchain's sysroot-only search.
    find_program(
        HelloAndroidCamera2_NATIVE_CXX REQUIRED
        NAMES c++ clang++ g++
        NO_CMAKE_FIND_ROOT_PATH
    )

    execute_process(
        COMMAND
            ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR} -B
            ${HelloAndroidCamera2-halide_generators_ROOT}
            -DCMAKE_CXX_COMPILER=${HelloAndroidCamera2_NATIVE_CXX}
            -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
        COMMAND_ERROR_IS_FATAL ANY
    )

    execute_process(
        COMMAND ${CMAKE_COMMAND} --build ${HelloAndroidCamera2-halide_generators_ROOT}
        COMMAND_ERROR_IS_FATAL ANY
    )
endif ()

# Halide is a host-built config package, not part of the Android sysroot.
find_package(Halide REQUIRED CMAKE_FIND_ROOT_PATH_BOTH)

add_halide_generator(deinterleave_generator SOURCES generators/deinterleave_generator.cpp)
add_halide_generator(edge_detect_generator SOURCES generators/edge_detect_generator.cpp)

if (NOT ANDROID)
    # We're just building the Halide generators.
    return()
endif ()

add_halide_library(deinterleave FROM HelloAndroidCamera2::halide_generators::deinterleave_generator)
add_halide_library(edge_detect FROM HelloAndroidCamera2::halide_generators::edge_detect_generator)

add_library(
    HelloAndroidCamera2 SHARED
    AndroidBufferUtilities.cpp
    HalideFilters.cpp
    LockedSurface.cpp
    YuvBufferT.cpp
)
target_link_libraries(HelloAndroidCamera2 PRIVATE deinterleave edge_detect log android atomic)
