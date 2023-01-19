cmake_minimum_required(VERSION 3.22)

project(HelloBaremetal-gen)

if (NOT DEFINED GEN_EXE)
    message(FATAL_ERROR "CMakeVariable GEN_EXE is not set")
endif()

# Set up C++ language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Halide REQUIRED)  # We need Halide::Generator

# Build generator
add_halide_generator(${GEN_EXE}
                     SOURCES ${SRC_DIR}/add_generator.cpp)
