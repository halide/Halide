#
# NOTE: Unlike all other CMakeLists.txt in the apps/ folder, this 
# is deliberately intended to be standalone (not included from the toplevel)
# in order to show the minimum scaffolding necessary to use ahead-of-time 
# Generators in a simple app.
#
# To use:
# mkdir build_cmake && cd build_cmake && cmake .. && make -j8

project(wavelet)
cmake_minimum_required(VERSION 2.8.12)

include(../../HalideGenerator.cmake)

# HalideGenerator.cmake makes this assumptions
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include_directories ("${CMAKE_CURRENT_LIST_DIR}/../support")
include_directories("${CMAKE_CURRENT_LIST_DIR}/../../include")

add_definitions("-std=c++11")
add_definitions("-fno-rtti")

add_executable(wavelet "${CMAKE_CURRENT_SOURCE_DIR}/wavelet.cpp")

find_package(PNG)
target_link_libraries(wavelet ${PNG_LIBRARIES})
target_include_directories(wavelet PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" ${PNG_INCLUDE_DIRS})
target_compile_definitions(wavelet PRIVATE ${PNG_DEFINITIONS})

# GENS := *_generator.cpp
file(GLOB GENS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*_generator.cpp")

foreach(GEN_SRC ${GENS})
    # GEN_NAME := *.generator
    string(REPLACE "_generator.cpp" "" GEN_NAME "${GEN_SRC}")
    # GEN_EXECUTABLE := *.generator
    string(REPLACE "_generator.cpp" "" GEN_EXECUTABLE "${GEN_SRC}.generator")
    add_executable("${GEN_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/GenGen.cpp" "${GEN_SRC}")
    target_link_libraries("${GEN_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../../bin/libHalide.a" z)

    halide_add_generator_dependency(TARGET wavelet
                           GENERATOR_TARGET "${GEN_EXECUTABLE}"
                           GENERATOR_NAME "${GEN_NAME}"
                           GENERATED_FUNCTION "${GEN_NAME}"
                           GENERATOR_ARGS "target=host")
endforeach()


