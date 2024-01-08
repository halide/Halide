cmake_minimum_required(VERSION 3.22...3.23)
project(Halide
        VERSION 17.0.0
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

    set(Halide_CCACHE_PARAMS
        CCACHE_CPP2=yes
        CCACHE_HASHDIR=yes
        CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime
        CACHE STRING "Parameters to pass through to ccache")
    mark_as_advanced(Halide_CCACHE_PARAMS)

    set(CMAKE_C_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env ${Halide_CCACHE_PARAMS} ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env ${Halide_CCACHE_PARAMS} ${CCACHE_PROGRAM})

    # Per https://ccache.dev/manual/latest.html#_precompiled_headers,
    # we must set -fno-pch-timestamp when using Clang + CCache + PCH
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        string(APPEND CMAKE_C_FLAGS " -Xclang -fno-pch-timestamp")
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        string(APPEND CMAKE_CXX_FLAGS " -Xclang -fno-pch-timestamp")
    endif()

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
option(TARGET_VULKAN "Include Vulkan target" ON)
if (TARGET_VULKAN)
    set(TARGET_SPIRV ON) # required
endif()

# Helper function to set C++ compiler warnings in a sane way
function(set_halide_compiler_warnings NAME)
    target_compile_options(
        ${NAME}
        PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall>

        # variable length arrays in C++ are a Clang extension, we don't want to use them
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wvla-extension>

        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wcast-qual>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wignored-qualifiers>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Woverloaded-virtual>

        $<$<CXX_COMPILER_ID:GNU>:-Wsuggest-override>

        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Winconsistent-missing-destructor-override>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Winconsistent-missing-override>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wdeprecated-declarations>

        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-double-promotion>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-float-conversion>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-float-equal>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-missing-field-initializers>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-old-style-cast>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-shadow>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-sign-conversion>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-switch-enum>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-undef>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-unused-function>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-unused-macros>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-unused-parameter>

        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-c++98-compat-pedantic>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-c++98-compat>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-cast-align>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-comma>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-covered-switch-default>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-documentation-unknown-command>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-documentation>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-exit-time-destructors>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-global-constructors>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-implicit-float-conversion>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-implicit-int-conversion>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-implicit-int-float-conversion>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-missing-prototypes>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-nonportable-system-include-path>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-reserved-id-macro>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-shadow-field-in-constructor>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-shadow-field>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-shorten-64-to-32>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-undefined-func-template>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-unused-member-function>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-unused-template>

        # This warning was removed in Clang 13
        $<$<AND:$<CXX_COMPILER_ID:Clang,AppleClang>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,13.0>>:-Wno-return-std-move-in-c++11>

        $<$<CXX_COMPILER_ID:MSVC>:/W3>
        $<$<CXX_COMPILER_ID:MSVC>:/wd4018>  # 4018: disable "signed/unsigned mismatch"
        $<$<CXX_COMPILER_ID:MSVC>:/wd4141>  # 4141: 'inline' used more than once
        $<$<CXX_COMPILER_ID:MSVC>:/wd4146>  # 4146: unary minus applied to unsigned type
        $<$<CXX_COMPILER_ID:MSVC>:/wd4244>  # 4244: conversion, possible loss of data
        $<$<CXX_COMPILER_ID:MSVC>:/wd4267>  # 4267: conversion from 'size_t' to 'int', possible loss of data
        $<$<CXX_COMPILER_ID:MSVC>:/wd4291>  # 4291: No matching operator delete found
        $<$<CXX_COMPILER_ID:MSVC>:/wd4503>  # 4503: disable "decorated name length exceeded, name was truncated"
        $<$<CXX_COMPILER_ID:MSVC>:/wd4800>  # 4800: forcing value to bool 'true' or 'false' (performance warning)

        # No: enable deprecation warnings
        # $<$<CXX_COMPILER_ID:MSVC>:/wd4996>  # 4996: compiler encountered deprecated declaration
    )
endfunction()


##
# Import dependencies
##

## Threads
option(THREADS_PREFER_PTHREAD_FLAG "When enabled, prefer to use the -pthread flag to explicit linking" ON)
find_package(Threads REQUIRED)

## Complex dependencies
add_subdirectory(dependencies)

## Image formats

# This changes how find_xxx() commands work; the default is to find frameworks before
# standard libraries or headers, but this can be a problem on systems that have Mono
# installed, as it has a framework with the libjpeg and libpng  headers present -- so
# CMake finds the headers from Mono but the libraries from Homebrew, and hilarity ensues.
# Setting this to "last" means we always try the standard libraries before the frameworks.
set(CMAKE_FIND_FRAMEWORK LAST)

# TODO: these really belong in tools/, but CMake has a weird bug with $<TARGET_NAME_IF_EXISTS:...>
# https://gitlab.kitware.com/cmake/cmake/-/issues/25033
find_package(JPEG)
find_package(PNG)

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
