cmake_minimum_required(VERSION 3.22...3.23)
project(Halide
        VERSION 16.0.0
        DESCRIPTION "Halide compiler and libraries"
        HOMEPAGE_URL "https://halide-lang.org")

enable_testing()

##
# Set up project-wide properties
##

# Import useful standard modules
include(CMakeDependentOption)
include(CheckCXXSymbolExists)

# Make our custom helpers available throughout the project via include().
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
include(HalideGeneratorHelpers)

# Build Halide as a shared lib by default, but still honor command-line settings.
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# Warn if the user did not set a build type and is using a single-configuration generator.
get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT IS_MULTI_CONFIG AND NOT DEFINED CMAKE_BUILD_TYPE)
    message(WARNING "Single-configuration generators require CMAKE_BUILD_TYPE to be set.")
endif ()

# Windows has file name length restrictions and lacks an RPATH mechanism.
# We work around this by setting a path max and putting all exes / dlls in
# the same output directory.
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    set(CMAKE_OBJECT_PATH_MAX 260)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")

    message(STATUS "Windows: setting CMAKE_OBJECT_PATH_MAX to ${CMAKE_OBJECT_PATH_MAX}")
endif ()

# Export all symbols on Windows to match GCC/Clang behavior on Linux/macOS
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# Require standard C++17
set(CMAKE_CXX_STANDARD 17 CACHE STRING "The C++ standard to use. Halide requires 17 or higher.")
option(CMAKE_CXX_STANDARD_REQUIRED "When enabled, the value of CMAKE_CXX_STANDARD is a requirement." ON)
option(CMAKE_CXX_EXTENSIONS "When enabled, compiler-specific language extensions are enabled (e.g. -std=gnu++17)" OFF)

if(CMAKE_CXX_STANDARD LESS 17)
    message(FATAL_ERROR "Halide requires C++17 or newer but CMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}")
endif()

# Build Halide with ccache if the package is present
option(Halide_CCACHE_BUILD "Set to ON for a ccache enabled build" OFF)
mark_as_advanced(Halide_CCACHE_BUILD)

if (Halide_CCACHE_BUILD)
    find_program(CCACHE_PROGRAM ccache REQUIRED)

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
endif ()

# Detect whether or not ASAN is enabled. Don't cache the result to ensure this
# check happens every time we reconfigure.
unset(Halide_ASAN_ENABLED CACHE)
check_cxx_symbol_exists(HALIDE_INTERNAL_USING_ASAN "${Halide_SOURCE_DIR}/src/Util.h" Halide_ASAN_ENABLED)

if (Halide_ASAN_ENABLED)
    set(Halide_ANY_SANITIZERS_ENABLED 1)
else ()
    set(Halide_ANY_SANITIZERS_ENABLED 0)
endif ()

# Enable the SPIR-V target if requested (must declare before processing dependencies)
option(TARGET_SPIRV "Include SPIR-V target" OFF)

##
# Import dependencies
##

add_subdirectory(dependencies)

##
# Declare options
##

# Declare these options after we include dependencies (since it declares Halide_ENABLE_RTTI etc)
# but before we add any subdirectories, since any option you test before it is defined is
# implicitly false the *first* time that the build file is processed, and there are some
# out-of-order dependencies here (e.g, code in src/ eventually checks WITH_UTILS).
# This is especially subtle since it means that some options can end up with different
# values if you build a target as part of the initial CMake run, so (e.g.) a `make install`
# from as totally clean build might neglect to install some pieces.

option(WITH_TESTS "Build tests" "${PROJECT_IS_TOP_LEVEL}")
option(WITH_TUTORIALS "Build tutorials" "${PROJECT_IS_TOP_LEVEL}")
option(WITH_DOCS "Build documentation" OFF)
option(WITH_UTILS "Build utils" "${PROJECT_IS_TOP_LEVEL}")
cmake_dependent_option(
    WITH_PYTHON_BINDINGS "Build Python bindings" "${PROJECT_IS_TOP_LEVEL}"
    "Halide_ENABLE_RTTI AND Halide_ENABLE_EXCEPTIONS" OFF
)


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
    message(STATUS "Building Python bindings enabled")
    add_subdirectory(python_bindings)
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

add_subdirectory(packaging)
