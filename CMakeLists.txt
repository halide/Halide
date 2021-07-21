cmake_minimum_required(VERSION 3.16...3.20)
project(Halide
        VERSION 13.0.0
        DESCRIPTION "Halide compiler and libraries"
        HOMEPAGE_URL "https://halide-lang.org")

enable_testing()

##
# Set up project-wide properties
##

# Make our custom helpers available throughout the project via include().
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
include(HalideGeneratorHelpers)
include(MakeShellPath)
include(CMakeDependentOption)

# Preferred defaults for standard CMake settings
set(CMAKE_CXX_STANDARD 14 CACHE STRING "Default minimum C++ standard")
option(CMAKE_CXX_STANDARD_REQUIRED "Disable CMake standard-version-decay behavior" ON)
option(CMAKE_CXX_EXTENSIONS "Compile with non-standard C++ extensions (e.g. gnu++17)" OFF)
option(BUILD_SHARED_LIBS "Build shared libs by default" YES)

# Set to LAST by default since Mono has a framework with the libjpeg
# and libpng headers present -- so CMake finds the headers from Mono
# but the libraries from Homebrew, and hilarity ensues.
set(CMAKE_FIND_FRAMEWORK LAST
    CACHE STRING "Make find_* commands search for macOS Frameworks FIRST/LAST/ONLY/NEVER")

# Warn if the user did not set a build type and is using a single-configuration generator.
get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT is_multi_config AND NOT CMAKE_BUILD_TYPE)
    message(WARNING "Single-configuration generators require CMAKE_BUILD_TYPE to be set.")
endif ()

# Build Halide with ccache if the package is present
option(Halide_CCACHE_BUILD "Set to ON for a ccache enabled build" OFF)
if (Halide_CCACHE_BUILD)
    find_program(CCACHE ccache)
    if (CCACHE)
        # TODO: ccache recommends setting CCACHE_SLOPPINESS=pch_defines,time_macros to
        # enable precompiled header caching. Our timing found it slightly faster with
        # just CCACHE_SLOPPINESS=pch_defines, so that's what we're using. Maybe revisit
        # if issues occur (but we don't use any of the time macros so should be irrelevant).
        set(Halide_CCACHE_PARAMS CCACHE_CPP2=yes CCACHE_HASHDIR=yes CCACHE_SLOPPINESS=pch_defines
            CACHE STRING "Parameters to pass through to ccache")
        set(CMAKE_C_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env ${Halide_CCACHE_PARAMS} ${CCACHE})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env ${Halide_CCACHE_PARAMS} ${CCACHE})
        message(STATUS "Enabling ccache usage for building.")
    else ()
        message(FATAL_ERROR "Unable to find the program ccache. Set Halide_CCACHE_BUILD to OFF")
    endif ()
endif ()

##
# Import dependencies
##

add_subdirectory(dependencies)

find_package(Threads REQUIRED)
find_package(OpenMP)

# TODO(https://github.com/halide/Halide/issues/5633): verify this is still correct / necessary for OpenGLCompute
find_package(OpenGL)

find_package(JPEG)
find_package(PNG)

##
# Add source directories
##

add_subdirectory(tools)
add_subdirectory(src)

##
# Add tests, tutorials, etc. if we're not being imported into another CMake project.
##

function(add_subproject dir condition)
    if (${condition})
        message(STATUS "Adding subproject ${dir}")
        add_subdirectory(${dir})
    else ()
        message(STATUS "Skipping subproject ${dir}")
    endif ()
endfunction()

string(COMPARE EQUAL "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" is_top_level)

option(WITH_TESTS "Build tests" "${is_top_level}")
add_subproject(test WITH_TESTS)

option(WITH_PYTHON_BINDINGS "Build Python bindings" "${is_top_level}")
add_subproject(python_bindings WITH_PYTHON_BINDINGS)

option(WITH_TUTORIALS "Build tutorials" "${is_top_level}")
add_subproject(tutorial WITH_TUTORIALS)

option(WITH_DOCS "Build documentation" OFF)
add_subproject(doc WITH_DOCS)

option(WITH_UTILS "Build utils" "${is_top_level}")
add_subproject(util WITH_UTILS)

option(WITH_PACKAGING "Add packaging to the build" "${is_top_level}")
add_subproject(packaging WITH_PACKAGING)
