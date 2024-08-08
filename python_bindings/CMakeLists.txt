cmake_minimum_required(VERSION 3.28)
project(Halide_Python)

if (PROJECT_IS_TOP_LEVEL)
    enable_testing()
endif ()

include(CMakeDependentOption)

##
# Project options
##

# Preferred defaults for built-in options
set(CMAKE_CXX_STANDARD 17 CACHE STRING "The minimum C++ standard to use")
option(CMAKE_CXX_STANDARD_REQUIRED "Prevent CMake C++ standard selection decay" ON)
option(CMAKE_CXX_EXTENSIONS "Enable C++ vendor extensions (e.g. GNU)" OFF)

# Support not actually building the bindings, but using the ones we find
# via `find_package(Halide)`. This allows running tests against the
# installed Halide package.
option(WITH_PYTHON_BINDINGS "Build Python bindings" ON)

# Duplicated options from parent project
option(WITH_TESTS "Build tests" ON)
option(WITH_TUTORIALS "Build tutorials" ON)
option(WITH_PYTHON_STUBS "Build Python stubs" ON)

# Enable/disable testing
cmake_dependent_option(
    WITH_TEST_PYTHON "Build Python tests" ON
    WITH_TESTS OFF
)

##
# Dependencies
##

# The plain Development component is the same as requesting both
# Development.Module and Development.Embed. We don't need the Embed
# part, so only requesting Module avoids failures when Embed is not
# available, as is the case in the manylinux Docker images.
find_package(Python3 3.8 REQUIRED Interpreter Development.Module)

if (WITH_PYTHON_BINDINGS)
    find_package(pybind11 2.10.4 REQUIRED)
endif ()

# Note: this must happen, especially when WITH_PYTHON_BINDINGS is OFF.
find_package(Halide REQUIRED Halide)
if (NOT Halide_ENABLE_RTTI OR NOT Halide_ENABLE_EXCEPTIONS)
    message(FATAL_ERROR "Python bindings require RTTI and exceptions to be enabled.")
endif ()

##
# A helper for creating tests with correct PYTHONPATH and sanitizer preloading
##

function(add_python_test)
    cmake_parse_arguments(ARG "" "FILE;LABEL" "PYTHONPATH;ENVIRONMENT;TEST_ARGS" ${ARGN})

    list(PREPEND ARG_PYTHONPATH "$<TARGET_FILE_DIR:Halide::Python>/..")
    list(TRANSFORM ARG_PYTHONPATH PREPEND "PYTHONPATH=path_list_prepend:")

    list(PREPEND ARG_ENVIRONMENT "HL_TARGET=${Halide_TARGET};HL_JIT_TARGET=${Halide_TARGET}")

    cmake_path(GET ARG_FILE STEM test_name)
    set(test_name "${ARG_LABEL}_${test_name}")

    add_test(
        NAME "${test_name}"
        COMMAND ${Halide_PYTHON_LAUNCHER} "$<TARGET_FILE:Python3::Interpreter>" "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/${ARG_FILE}>" ${ARG_TEST_ARGS}
    )
    set_tests_properties(
        "${test_name}"
        PROPERTIES
        LABELS "python"
        ENVIRONMENT "${ARG_ENVIRONMENT}"
        ENVIRONMENT_MODIFICATION "${ARG_PYTHONPATH}"
    )
endfunction()


##
# Add our sources to this sub-tree.
##

if (WITH_PYTHON_BINDINGS)
    add_subdirectory(src)
endif ()

if (WITH_PYTHON_BINDINGS AND WITH_PYTHON_STUBS)
    add_subdirectory(stub)
endif ()

if (WITH_TEST_PYTHON)
    add_subdirectory(apps)
    add_subdirectory(test)
endif ()

if (WITH_TUTORIALS)
    add_subdirectory(tutorial)
endif ()
