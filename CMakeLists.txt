cmake_minimum_required(VERSION 3.22...3.23)
project(Halide
        VERSION 16.0.0
        DESCRIPTION "Halide compiler and libraries"
        HOMEPAGE_URL "https://halide-lang.org")

enable_testing()

##
# Required CMake modules
##

# Import useful standard modules
include(CMakeDependentOption)
include(CheckCXXSymbolExists)

# Make our custom helpers available throughout the project via include().
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
include(HalideGeneratorHelpers)

get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

# Warn if the user is using the wrong kind of build type selection for their generator
if (CMAKE_BUILD_TYPE AND is_multi_config)
    message(WARNING "Multi-config generators do not honor CMAKE_BUILD_TYPE.")
elseif (NOT is_multi_config AND NOT DEFINED CMAKE_BUILD_TYPE)
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

##
# Settings for finding dependencies
##

option(THREADS_PREFER_PTHREAD_FLAG "Enable to use the -pthreads flag when available." ON)

if (APPLE)
    # This changes how find_xxx() commands work; the default is to find frameworks before
    # standard libraries or headers, but this can be a problem on systems that have Mono
    # installed, as it has a framework with the libjpeg and libpng  headers present -- so
    # CMake finds the headers from Mono but the libraries from Homebrew, and hilarity ensues.
    # Setting this to "last" means we always try the standard libraries before the frameworks.
    set(CMAKE_FIND_FRAMEWORK "LAST"
        CACHE STRING "Whether to search for macOS Frameworks FIRST, LAST, ONLY, or NEVER.")
endif ()

# LLVM link modes
option(Halide_SHARED_LLVM "Enable to link to shared LLVM (Linux only)" OFF)
cmake_dependent_option(Halide_BUNDLE_LLVM "Enable with BUILD_SHARED_LIBS=OFF to bundle LLVM libs with static Halide" OFF
                       "NOT BUILD_SHARED_LIBS" OFF)

# LLVM target dependencies
set(known_llvm_targets AArch64 AMDGPU ARM Hexagon Mips NVPTX PowerPC RISCV WebAssembly X86)
set(required_llvm_targets "")
set(optional_llvm_targets "")
foreach (target IN LISTS known_llvm_targets)
    string(TOUPPER "TARGET_${target}" target_opt)

    set("${target_opt}" "OPTIONAL"
        CACHE STRING "Enable ${target} target. Set to 'OPTIONAL' to include if available (the default), or to a true value to require it.")

    if (${target_opt} STREQUAL "OPTIONAL")
        list(APPEND optional_llvm_targets "${target}")
    elseif (${target_opt})
        list(APPEND required_llvm_targets "${target}")
    endif ()
endforeach ()

# WASM dependency options

cmake_dependent_option(WITH_WABT "Include WABT Interpreter for WASM testing" ON
                       "NOT WIN32" OFF)

option(WITH_V8 "Include V8 for WASM testing" OFF)

# Validate dependency options
if (WITH_WABT AND WITH_V8)
    message(FATAL_ERROR "Cannot use both WABT and V8 at the same time, disable one of them.")
endif ()

##
# Find dependencies
##

## Make our custom helpers available via include() and find_package().
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

## Find standard CMake packages
find_package(Threads REQUIRED)
find_package(OpenGL)
find_package(OpenMP)
find_package(JPEG)
find_package(PNG)

## Find LLVM
find_package(HalideLLVM ${Halide_REQUIRE_LLVM_VERSION} REQUIRED
             COMPONENTS Clang ${required_llvm_targets}
             OPTIONAL_COMPONENTS ${optional_llvm_targets})

# Set TARGET_<target> to ON if it was actually found and to OFF otherwise.
# Does NOT touch the cache; if it was marked "optional" before, it will
# still be marked optional afterwards.
foreach (target IN LISTS known_llvm_targets)
    string(TOUPPER "TARGET_${target}" target_opt)
    if (${target} IN_LIST HalideLLVM_TARGETS)
        set("${target_opt}" ON)
    else ()
        set("${target_opt}" OFF)
    endif ()
endforeach ()

## WebAssembly target requires wasm
if (TARGET_WEBASSEMBLY)
    if (WITH_WABT)
        find_package(WABT REQUIRED)
    endif ()

    if (WITH_V8)
        find_package(V8 REQUIRED)
    endif ()
endif ()

##
# Define project-wide options
##

## C++ standard selection
set(CMAKE_CXX_STANDARD "17" CACHE STRING "Select which C++ standard to use (>=C++17 required)")
option(CMAKE_CXX_STANDARD_REQUIRED "Enable strict C++ standard selection" ON)
option(CMAKE_CXX_EXTENSIONS "Enable compiler-specific C++ extensions" OFF)

## C++ feature selection
option(Halide_ENABLE_RTTI "Enable RTTI" "${LLVM_ENABLE_RTTI}")
option(Halide_ENABLE_EXCEPTIONS "Enable exceptions" ON)

## Library type selection (shared by default)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

## Main library features and backends
option(Halide_WITH_INTROSPECTION "Enable use of debugging symbols for default Func, Var, etc. names" ON)
option(TARGET_OPENCL "Enable OpenCL-C target" ON)
option(TARGET_METAL "Enable Metal target" ON)
option(TARGET_D3D12COMPUTE "Enable Direct3D 12 Compute target" ON)
option(TARGET_OPENGLCOMPUTE "Enable OpenGLCompute target" ON)
option(TARGET_SPIRV "Include SPIR-V target" OFF)

## Optional extra subprojects
option(WITH_TESTS "Build tests" "${PROJECT_IS_TOP_LEVEL}")
option(WITH_PYTHON_BINDINGS "Build Python bindings" "${PROJECT_IS_TOP_LEVEL}")
option(WITH_TUTORIALS "Build tutorials" "${PROJECT_IS_TOP_LEVEL}")
option(WITH_DOCS "Build documentation" OFF)
option(WITH_UTILS "Build utils" "${PROJECT_IS_TOP_LEVEL}")
option(WITH_PACKAGING "Include Halide's packaging rules" "${PROJECT_IS_TOP_LEVEL}")

## Advanced options for special/internal build scenarios
option(Halide_CCACHE_BUILD "Build Halide with ccache and preferred settings" OFF)
mark_as_advanced(Halide_CCACHE_BUILD)

option(Halide_CLANG_TIDY_BUILD "Generate fake compile jobs for runtime files when running clang-tidy." OFF)
mark_as_advanced(Halide_CLANG_TIDY_BUILD)

set(Halide_SOVERSION_OVERRIDE "${Halide_VERSION_MAJOR}"
    CACHE STRING "SOVERSION to set for custom Halide packaging")
mark_as_advanced(Halide_SOVERSION_OVERRIDE)

##
# Apply project-wide options
##

## Validate language standard settings
if (CMAKE_CXX_STANDARD LESS 17)
    message(FATAL_ERROR "Halide requires C++17 or newer but CMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}")
endif ()

## Validate RTTI compatibility with LLVM
if (Halide_ENABLE_RTTI AND NOT LLVM_ENABLE_RTTI)
    message(FATAL_ERROR "Can't enable RTTI. LLVM was compiled without it")
endif ()

## Enable CCache build when requested by the user (for convenience)
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

## Detect whether or not ASAN is enabled. Don't cache the result to ensure this
## check happens every time we reconfigure.
unset(Halide_ASAN_ENABLED CACHE)
check_cxx_symbol_exists(HALIDE_INTERNAL_USING_ASAN "${Halide_SOURCE_DIR}/src/Util.h" Halide_ASAN_ENABLED)

if (Halide_ASAN_ENABLED)
    set(Halide_ANY_SANITIZERS_ENABLED 1)
else ()
    set(Halide_ANY_SANITIZERS_ENABLED 0)
endif ()

## Find SPIR-V dependency if that backend was requested
if (TARGET_SPIRV)
    find_package(SPIRV-Headers REQUIRED)
endif ()

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

if (WITH_PACKAGING)
    message(STATUS "Halide packaging rules enabled")
    add_subdirectory(packaging)
else ()
    message(STATUS "Halide packaging rules disabled")
endif ()
