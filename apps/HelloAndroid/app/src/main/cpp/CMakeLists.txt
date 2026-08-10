cmake_minimum_required(VERSION 3.28)
project(HelloAndroid)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# This file configures itself twice: once as a host build (to compile the
# generator) and once as this target build (driven by AGP's
# externalNativeBuild, under the NDK cross toolchain). See "Use
# add_halide_generator" in doc/HalideCMakePackage.md.
set(HelloAndroid-halide_generators_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.cxx-generators")
if (ANDROID)
    # NO_CMAKE_FIND_ROOT_PATH: bypass the NDK toolchain's sysroot-only search.
    find_program(HelloAndroid_NATIVE_CXX REQUIRED NAMES c++ clang++ g++ NO_CMAKE_FIND_ROOT_PATH)

    execute_process(
        COMMAND
            ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR} -B ${HelloAndroid-halide_generators_ROOT}
            -DCMAKE_CXX_COMPILER=${HelloAndroid_NATIVE_CXX}
            -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
        COMMAND_ERROR_IS_FATAL ANY
    )

    execute_process(
        COMMAND ${CMAKE_COMMAND} --build ${HelloAndroid-halide_generators_ROOT}
        COMMAND_ERROR_IS_FATAL ANY
    )
endif ()

# Halide is a host-built config package, not part of the Android sysroot.
find_package(Halide REQUIRED CMAKE_FIND_ROOT_PATH_BOTH)

add_halide_generator(hello_generator SOURCES generators/hello_generator.cpp)

if (NOT ANDROID)
    # We're just building the Halide generators.
    return()
endif ()

add_halide_library(hello FROM HelloAndroid::halide_generators::hello_generator)

add_library(HelloAndroid SHARED hello_wrapper.cpp)
target_link_libraries(HelloAndroid PRIVATE hello log android)
