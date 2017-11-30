#
# NOTE: Unlike all other CMakeLists.txt in the apps/ folder, this
# is deliberately intended to be standalone (not included from the toplevel)
# in order to show the minimum scaffolding necessary to use ahead-of-time
# Generators in a simple app.
#
# To use:
# mkdir cmake_build && cd cmake_build && cmake .. && make -j8 && ./bin/wavelet ../../images/gray.png .

project(wavelet)
cmake_minimum_required(VERSION 3.1.3)

# halide.cmake requires that HALIDE_DISTRIB_DIR be set to point to the Halide distribution folder we use.
# This assumes it's built at the toplevel via 'make distrib' (ironically, the CMake build isn't
# yet capable of producing a distrib folder).
set(HALIDE_DISTRIB_DIR "${CMAKE_CURRENT_LIST_DIR}/../../distrib")

# halide.cmake defaults to using the shared-library version of libHalide;
# we can make it prefer the static-library version by setting this:
# set(HALIDE_DISTRIB_USE_STATIC_LIBRARY TRUE)

include("${HALIDE_DISTRIB_DIR}/halide.cmake")

# Define the wavelet app
add_executable(wavelet "${CMAKE_CURRENT_SOURCE_DIR}/wavelet.cpp")
set_target_properties(wavelet PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
target_include_directories(wavelet PRIVATE "${HALIDE_INCLUDE_DIR}" "${HALIDE_TOOLS_DIR}")
halide_use_image_io(wavelet)

# Define a halide_library() for each generator we have, and link each one into wavelet
file(GLOB GENS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*_generator.cpp")
foreach(GEN_SRC ${GENS})
    string(REPLACE "_generator.cpp" "" GEN_NAME "${GEN_SRC}")
    halide_library("${GEN_NAME}" SRCS ${GEN_SRC})
    target_link_libraries(wavelet PUBLIC "${GEN_NAME}")
endforeach()

