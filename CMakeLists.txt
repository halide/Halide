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

# Build Halide as a shared lib by default, but still honor command-line settings.
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
# Preferred defaults for standard CMake settings
set(CMAKE_CXX_STANDARD 17 CACHE STRING "Default minimum C++ standard")
option(CMAKE_CXX_STANDARD_REQUIRED "Disable CMake standard-version-decay behavior" ON)
option(CMAKE_CXX_EXTENSIONS "Compile with non-standard C++ extensions (e.g. gnu++17)" OFF)
option(BUILD_SHARED_LIBS "Build shared libs by default" ON)

# Warn if the user selected a low standard version
if (CMAKE_CXX_STANDARD LESS 17)
    message(WARNING "Halide requires C++17 and above.")
endif ()

# Warn if the user did not set a build type and is using a single-configuration generator.
get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT is_multi_config AND NOT CMAKE_BUILD_TYPE)
    message(WARNING "Single-configuration generators require CMAKE_BUILD_TYPE to be set.")
endif ()

# Build Halide with ccache if the package is present
option(Halide_CCACHE_BUILD "Set to ON for a ccache enabled build" OFF)
if (Halide_CCACHE_BUILD)
    find_program(CCACHE_PROGRAM ccache)
    if (CCACHE_PROGRAM)
        # TODO: ccache recommends setting CCACHE_SLOPPINESS=pch_defines,time_macros to
        # enable precompiled header caching. Our timing found it slightly faster with
        # just CCACHE_SLOPPINESS=pch_defines, so that's what we're using. Maybe revisit
        # if issues occur (but we don't use any of the time macros so should be irrelevant).
        set(Halide_CCACHE_PARAMS CCACHE_CPP2=yes CCACHE_HASHDIR=yes CCACHE_SLOPPINESS=pch_defines
            CACHE STRING "Parameters to pass through to ccache")
        set(CMAKE_C_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env ${Halide_CCACHE_PARAMS} ${CCACHE_PROGRAM})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env ${Halide_CCACHE_PARAMS} ${CCACHE_PROGRAM})
        message(STATUS "Enabling ccache usage for building.")
    else ()
        message(FATAL_ERROR "Unable to find the program ccache. Set Halide_CCACHE_BUILD to OFF")
    endif ()
endif ()

##
# Import dependencies
##

add_subdirectory(dependencies)

##
# Add source directories
##

add_subdirectory(src)
add_subdirectory(tools)

##
# Add tests, tutorials, etc. if we're not being imported into another CMake project.
##

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    option(WITH_TESTS "Build tests" ON)
    if (WITH_TESTS)
        message(STATUS "Building tests enabled")
        add_subdirectory(test)
    else ()
        message(STATUS "Building tests disabled")
    endif ()

    option(WITH_PYTHON_BINDINGS "Build Python bindings" ON)
    if (WITH_PYTHON_BINDINGS)
        if (Halide_ENABLE_RTTI AND Halide_ENABLE_EXCEPTIONS)
            message(STATUS "Building Python bindings enabled")
            add_subdirectory(python_bindings)
        else ()
            if (NOT Halide_ENABLE_RTTI)
                message(WARNING "Building Python bindings disabled: must compile with RTTI")
            endif ()
            if (NOT Halide_ENABLE_EXCEPTIONS)
                message(WARNING "Building Python bindings disabled: must compile with exceptions")
            endif ()
            set(WITH_PYTHON_BINDINGS OFF CACHE BOOL "Build Python bindings" FORCE)
        endif ()
    else ()
        message(STATUS "Building Python bindings disabled")
    endif ()

    option(WITH_TUTORIALS "Build tutorials" ON)
    if (WITH_TUTORIALS)
        message(STATUS "Building tutorials enabled")
        add_subdirectory(tutorial)
    else ()
        message(STATUS "Building tutorials disabled")
    endif ()

    option(WITH_DOCS "Build documentation" OFF)
    if (WITH_DOCS)
        message(STATUS "Building docs enabled")
        add_subdirectory(doc)
    else ()
        message(STATUS "Building docs disabled")
    endif ()

    option(WITH_UTILS "Build utils" ON)
    if (WITH_UTILS)
        message(STATUS "Building utils enabled")
        add_subdirectory(util)
    else ()
        message(STATUS "Building utils disabled")
    endif ()

    add_subdirectory(packaging)

    add_custom_target(distrib
                      COMMAND ${CMAKE_COMMAND} -E echo "\\'distrib\\' is not available under CMake. Use \\'package\\' instead.")

    if (TARGET clang-format AND NOT WIN32)
        add_custom_target(format COMMAND
                          find
                          "${Halide_SOURCE_DIR}/apps"
                          "${Halide_SOURCE_DIR}/src"
                          "${Halide_SOURCE_DIR}/tools"
                          "${Halide_SOURCE_DIR}/test"
                          "${Halide_SOURCE_DIR}/util"
                          "${Halide_SOURCE_DIR}/python_bindings"
                          -name *.cpp -o -name *.h -o -name *.c |
                          xargs $<TARGET_FILE:clang-format> -i -style=file)
    endif ()
endif ()
