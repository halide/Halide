# Include this file (optionally) via -DCMAKE_TOP_LEVEL_PROJECT_INCLUDES=/path/to/dependencies.cmake
# in order to redirect Halide's optional find_package calls to FetchContent calls.

cmake_minimum_required(VERSION 3.24)

include(FetchContent)

FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG 0bd8896a4010f2d91b2340570c24fa08606ec406  # v2.10.3
    GIT_SHALLOW TRUE
    GIT_SUBMODULES ""
)

FetchContent_Declare(
    SPIRV-Headers
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
    GIT_TAG 1d31a100405cf8783ca7a31e31cdd727c9fc54c3  # sdk-1.3.236.0
    GIT_SHALLOW TRUE
    GIT_SUBMODULES ""
)

FetchContent_Declare(
    wabt
    GIT_REPOSITORY https://github.com/WebAssembly/wabt.git
    GIT_TAG e5b042a72e6b6395e1d77901e7a413d6af87ae67  # 1.0.32
    GIT_SHALLOW TRUE
    GIT_SUBMODULES ""
)

function(Halide_provide_dependency method dep_name)
    if (dep_name STREQUAL "pybind11")
        FetchContent_MakeAvailable("${dep_name}")

        set(${dep_name}_FOUND TRUE PARENT_SCOPE)
    elseif (dep_name STREQUAL "SPIRV-Headers")
        FetchContent_MakeAvailable("${dep_name}")

        if (NOT TARGET SPIRV-Headers::SPIRV-Headers)
            add_library(SPIRV-Headers::SPIRV-Headers ALIAS SPIRV-Headers)
        endif ()

        set(${dep_name}_FOUND TRUE PARENT_SCOPE)
    elseif (dep_name STREQUAL "wabt")
        set(BUILD_TESTS 0)
        set(BUILD_TOOLS 0)
        set(BUILD_LIBWASM 0)

        if (BUILD_SHARED_LIBS)
            set(CMAKE_POSITION_INDEPENDENT_CODE ON)
        endif ()

        FetchContent_MakeAvailable("${dep_name}")

        set(${dep_name}_FOUND TRUE PARENT_SCOPE)
    endif ()
endfunction()

cmake_language(
    SET_DEPENDENCY_PROVIDER Halide_provide_dependency
    SUPPORTED_METHODS FIND_PACKAGE
)
