cmake_minimum_required(VERSION 3.16)
project(Halide VERSION 12.0.0)

enable_testing()

##
# Set up project-wide properties
##

# Make our custom helpers available throughout the project via include().
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
include(HalideTargetHelpers)
include(HalideGeneratorHelpers)
include(MakeShellPath)
include(CMakeDependentOption)

# Build Halide as a shared lib by default, but still honor command-line settings.
if (NOT DEFINED BUILD_SHARED_LIBS)
    set(BUILD_SHARED_LIBS YES)
endif ()

# Warn if the user did not set a build type and is using a single-configuration generator.
get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT IS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
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

# Require standard C++14
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
# Add tests, apps, tutorials, etc. if we're not being imported into another CMake project.
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

    cmake_dependent_option(WITH_APPS "Build apps" ON "BUILD_SHARED_LIBS" OFF)
    if (WITH_APPS)
        message(STATUS "Building apps enabled")
        add_subdirectory(apps)
    else ()
        message(STATUS "Building apps disabled")
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
