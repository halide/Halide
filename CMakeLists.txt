cmake_minimum_required(VERSION 3.28)

option(Halide_USE_FETCHCONTENT "When Halide is top-level, use FetchContent for build-time dependencies." ON)
if (Halide_USE_FETCHCONTENT)
    list(APPEND CMAKE_PROJECT_TOP_LEVEL_INCLUDES "${CMAKE_CURRENT_LIST_DIR}/cmake/dependencies.cmake")
endif ()

# TODO: remove this after updating build bots.
if (NOT DEFINED VCPKG_OVERLAY_PORTS)
    set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/cmake/vcpkg")
endif ()

# TODO: remove this after updating build bots.
if (NOT DEFINED VCPKG_MANIFEST_FEATURES)
    set(VCPKG_MANIFEST_FEATURES developer)
endif ()

project(Halide
        VERSION 19.0.0
        DESCRIPTION "Halide compiler and libraries"
        HOMEPAGE_URL "https://halide-lang.org")

enable_testing()

##
# Disable find_package(Halide) inside the build
##

file(CONFIGURE OUTPUT "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/HalideConfig.cmake"
     CONTENT [[set(Halide_FOUND 1)
               set(Halide_VERSION @Halide_VERSION@)]])

file(CONFIGURE OUTPUT "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/HalideHelpersConfig.cmake"
     CONTENT "set(HalideHelpers_FOUND 1)\n")

##
# Set up project-wide properties
##

# Import useful standard modules
include(CheckCXXSymbolExists)

# Make our custom helpers available throughout the project via include().
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
include(BundleStatic)
include(HalideFeatures)
include(HalideGeneratorHelpers)
include(HalidePackageConfigHelpers)

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

if (CMAKE_CXX_STANDARD LESS 17)
    message(FATAL_ERROR "Halide requires C++17 or newer but CMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}")
endif ()

# Build Halide with ccache if the package is present and the user requested it
Halide_feature(Halide_CCACHE_BUILD "Build with CCache as best configured for Halide" OFF ADVANCED)
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
    if (CMAKE_C_COMPILER_ID MATCHES "Clang")
        string(APPEND CMAKE_C_FLAGS " -Xclang -fno-pch-timestamp")
    endif ()
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        string(APPEND CMAKE_CXX_FLAGS " -Xclang -fno-pch-timestamp")
    endif ()
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
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wimplicit-fallthrough>

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

## LLVM
find_package(Halide_LLVM 17...20 REQUIRED
             COMPONENTS WebAssembly X86
             OPTIONAL_COMPONENTS AArch64 ARM Hexagon NVPTX PowerPC RISCV)

_Halide_pkgdep(Halide_LLVM PACKAGE_VARS Halide_LLVM_SHARED_LIBS)

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
# Optional features. These settings are defined early so that subdirectories see a consistent view

Halide_feature(Halide_BUNDLE_STATIC "Bundle Halide's static dependencies" OFF ADVANCED
               DEPENDS NOT BUILD_SHARED_LIBS)

Halide_feature(Halide_ENABLE_EXCEPTIONS "Enable exceptions in Halide" ON)
Halide_feature(Halide_ENABLE_RTTI "Enable RTTI in Halide" ON
               DEPENDS LLVM_ENABLE_RTTI)
Halide_feature(Halide_BUNDLE_STATIC "Bundle Halide's static dependencies" OFF ADVANCED
               DEPENDS NOT BUILD_SHARED_LIBS)

Halide_feature(WITH_AUTOSCHEDULERS "Build the Halide autoschedulers" ON
               DEPENDS BUILD_SHARED_LIBS)
Halide_feature(WITH_DOCS "Halide's Doxygen documentation" OFF)
Halide_feature(WITH_PACKAGING "Halide's CMake package install rules" TOP_LEVEL)
Halide_feature(WITH_PYTHON_BINDINGS "Halide's native Python module (not the whole pip package)" ON
               DEPENDS Halide_ENABLE_EXCEPTIONS AND Halide_ENABLE_RTTI)
Halide_feature(WITH_SERIALIZATION "Include experimental Serialization/Deserialization code" ON)
Halide_feature(WITH_SERIALIZATION_JIT_ROUNDTRIP_TESTING
               "Intercepting JIT compilation with a serialization roundtrip, for test only"
               OFF ADVANCED
               DEPENDS WITH_SERIALIZATION)
Halide_feature(WITH_TESTS "Halide's unit test suite" TOP_LEVEL)
Halide_feature(WITH_TUTORIALS "Halide's tutorial code" TOP_LEVEL)
Halide_feature(WITH_UTILS "Optional utility programs for Halide, including HalideTraceViz" TOP_LEVEL)

##
# Add source directories

add_subdirectory(src)
add_subdirectory(tools)

if (WITH_TESTS)
    add_subdirectory(test)
endif ()

if (WITH_PYTHON_BINDINGS)
    add_subdirectory(python_bindings)
endif ()

if (WITH_TUTORIALS)
    add_subdirectory(tutorial)
endif ()

if (WITH_DOCS)
    add_subdirectory(doc)
endif ()

if (WITH_UTILS)
    add_subdirectory(util)
endif ()

if (WITH_PACKAGING)
    add_subdirectory(packaging)
endif ()
