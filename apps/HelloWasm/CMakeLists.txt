cmake_minimum_required(VERSION 3.28)
project(HelloWasm-Super)

include(ExternalProject)

# Halide generators always run on the host, regardless of the target they
# emit code for -- only the final app (core.cpp, linked with em++) actually
# needs the Emscripten toolchain. This follows the "Use a super-build"
# pattern in doc/HalideCMakePackage.md (see also
# apps/HelloBaremetal/cmake-super_build/ for the same pattern applied to a
# baremetal cross-compile).

# Prefer $EMSDK (set by sourcing emsdk_env.sh); PATH_SUFFIXES is what lets this
# find emcc without the PATH.
find_program(EMCC REQUIRED NAMES emcc HINTS ENV EMSDK PATH_SUFFIXES "upstream/emscripten")
get_filename_component(EMSCRIPTEN_ROOT "${EMCC}" DIRECTORY)
find_file(
    EMSCRIPTEN_TOOLCHAIN_FILE REQUIRED
    NAMES Emscripten.cmake
    PATHS "${EMSCRIPTEN_ROOT}/cmake/Modules/Platform"
    NO_DEFAULT_PATH
)

set(GEN_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/generator")

# Default to Release (both sub-projects pick up -O3 this way, standard CMake
# practice, rather than hardcoding optimization flags).
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

# Step 1: build the generator (and the native benchmark) with the host compiler.
ExternalProject_Add(
    generator_project
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generator
    BINARY_DIR ${GEN_BINARY_DIR}
    INSTALL_COMMAND ""
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
    CONFIGURE_HANDLED_BY_BUILD TRUE
)

# Step 2: cross-compile the app with Emscripten, importing the generator
# built in step 1.
ExternalProject_Add(
    app_project
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/app
    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/app
    INSTALL_COMMAND ""
    DEPENDS generator_project
    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${EMSCRIPTEN_TOOLCHAIN_FILE}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
        -DHelloWasm-halide_generators_ROOT:FILEPATH=${GEN_BINARY_DIR}
    CONFIGURE_HANDLED_BY_BUILD TRUE
)

enable_testing()
add_test(
    NAME benchmarks
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${GEN_BINARY_DIR} --output-on-failure
)
