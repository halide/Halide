cmake_minimum_required(VERSION 3.28)
project(HelloBaremetal-gen)

# Set up C++ language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Halide REQUIRED)

# Build generator
add_halide_generator(add.generator SOURCES ${SRC_DIR}/add_generator.cpp)
