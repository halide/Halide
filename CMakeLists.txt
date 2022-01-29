cmake_minimum_required(VERSION 3.16...3.22)
project(Halide
        VERSION 14.0.0
        DESCRIPTION "Halide compiler and libraries"
        HOMEPAGE_URL "https://halide-lang.org")

enable_testing()

##
# Required CMake modules
##

include(CMakeDependentOption)
include(GNUInstallDirs)

##
# Gather information about build environment
##

get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
string(COMPARE EQUAL "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" is_top_level)

## Warn if the user is using the wrong kind of build type selection for their generator
if (CMAKE_BUILD_TYPE AND is_multi_config)
    message(WARNING "Multi-config generators do not honor CMAKE_BUILD_TYPE.")
elseif (NOT CMAKE_BUILD_TYPE AND NOT is_multi_config)
    message(WARNING "Single-config generators require CMAKE_BUILD_TYPE.")
endif ()

## Windows has file name length restrictions and lacks an RPATH mechanism.
## We work around this by setting a path max and putting all exes / dlls in
## the same output directory.
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    set(CMAKE_OBJECT_PATH_MAX 260)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")

    message(STATUS "Windows: setting CMAKE_OBJECT_PATH_MAX to ${CMAKE_OBJECT_PATH_MAX}")
endif ()

##
# Define project-wide options
##

## Language standard selection
set(CMAKE_CXX_STANDARD "17" CACHE STRING "Select which C++ standard to use (>=C++17 required)")
option(CMAKE_CXX_STANDARD_REQUIRED "Enable strict C++ standard selection" ON)
option(CMAKE_CXX_EXTENSIONS "Enable compiler-specific C++ extensions" OFF)

## Library type selection (shared by default)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

## Main library features and backends
option(Halide_WITH_INTROSPECTION "Enable use of debugging symbols for default Func, Var, etc. names" ON)
option(TARGET_OPENCL "Enable OpenCL-C target" ON)
option(TARGET_METAL "Enable Metal target" ON)
option(TARGET_D3D12COMPUTE "Enable Direct3D 12 Compute target" ON)
option(TARGET_OPENGLCOMPUTE "Enable OpenGLCompute target" ON)

## Optional extra subprojects
option(WITH_TESTS "Build tests" "${is_top_level}")
option(WITH_PYTHON_BINDINGS "Build Python bindings" "${is_top_level}")
option(WITH_TUTORIALS "Build tutorials" "${is_top_level}")
option(WITH_DOCS "Build documentation" OFF)
option(WITH_UTILS "Build utils" "${is_top_level}")
option(WITH_PACKAGING "Include Halide's packaging rules" "${is_top_level}")

## Advanced options for special/internal build scenarios
option(Halide_CCACHE_BUILD "Build Halide with ccache and preferred settings" OFF)
mark_as_advanced(Halide_CCACHE_BUILD)

option(Halide_CLANG_TIDY_BUILD "Generate fake compile jobs for runtime files when running clang-tidy." OFF)
mark_as_advanced(Halide_CLANG_TIDY_BUILD)

##
# Apply project-wide options
##

# Require standard C++17
if (CMAKE_CXX_STANDARD LESS 17)
    message(FATAL_ERROR "Halide requires C++17 or newer")
endif ()

# Configure CCache build when requested
if (Halide_CCACHE_BUILD)
    # TODO: when upgrading to CMake >=3.18, add REQUIRED here.
    find_program(CCACHE_PROGRAM ccache)
    if (CCACHE_PROGRAM)
        # TODO: ccache recommends setting CCACHE_SLOPPINESS=pch_defines,time_macros to
        # enable precompiled header caching. Our timing found it slightly faster with
        # just CCACHE_SLOPPINESS=pch_defines, so that's what we're using. Maybe revisit
        # if issues occur (but we don't use any of the time macros so should be irrelevant).
        set(Halide_CCACHE_PARAMS CCACHE_CPP2=yes CCACHE_HASHDIR=yes CCACHE_SLOPPINESS=pch_defines
            CACHE STRING "Parameters to pass through to ccache")
        mark_as_advanced(Halide_CCACHE_PARAMS)
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

# Make our custom helpers available throughout the project via include().
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

include(HalideGeneratorHelpers)
include(MakeShellPath)

add_subdirectory(dependencies)

##
# Add source directories
##

add_subdirectory(src)
add_subdirectory(tools)

##
# Add tests, tutorials, etc. if we're not being imported into another CMake project.
##

if (WITH_TESTS)
    message(STATUS "Building tests enabled")
    add_subdirectory(test)
else ()
    message(STATUS "Building tests disabled")
endif ()

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
    endif ()
else ()
    message(STATUS "Building Python bindings disabled")
endif ()

if (WITH_TUTORIALS)
    message(STATUS "Building tutorials enabled")
    add_subdirectory(tutorial)
else ()
    message(STATUS "Building tutorials disabled")
endif ()

if (WITH_DOCS)
    message(STATUS "Building docs enabled")
    add_subdirectory(doc)
else ()
    message(STATUS "Building docs disabled")
endif ()

if (WITH_UTILS)
    message(STATUS "Building utils enabled")
    add_subdirectory(util)
else ()
    message(STATUS "Building utils disabled")
endif ()

if (WITH_PACKAGING)
    message(STATUS "Halide packaging rules enabled")
    add_subdirectory(packaging)
else ()
    message(STATUS "Halide packaging rules disabled")
endif ()
