cmake_minimum_required(VERSION 3.22...3.23)
project(Halide_Python)

include(CMakeDependentOption)

##
# Project options
##

# Preferred defaults for built-in options
set(CMAKE_CXX_STANDARD 17 CACHE STRING "The minimum C++ standard to use")
option(CMAKE_CXX_STANDARD_REQUIRED "Prevent CMake C++ standard selection decay" ON)
option(CMAKE_CXX_EXTENSIONS "Enable C++ vendor extensions (e.g. GNU)" OFF)

# Duplicated options from parent project
option(WITH_TESTS "Build tests" ON)
option(WITH_TUTORIALS "Build tutorials" ON)

# Enable/disable testing
cmake_dependent_option(
    WITH_TEST_PYTHON "Build Python tests" ON
    WITH_TESTS OFF
)

# Set the expected (downloaded) version of pybind11
option(PYBIND11_USE_FETCHCONTENT "Enable to download pybind11 via FetchContent" ON)
set(PYBIND11_VER 2.6.2 CACHE STRING "The pybind11 version to use (or download)")

##
# Dependencies
##

find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

if (PYBIND11_USE_FETCHCONTENT)
    include(FetchContent)
    FetchContent_Declare(
        pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG v${PYBIND11_VER}
    )
    FetchContent_MakeAvailable(pybind11)
else ()
    find_package(pybind11 ${PYBIND11_VER} REQUIRED)
endif ()

find_package(Halide REQUIRED)
if (NOT Halide_ENABLE_RTTI OR NOT Halide_ENABLE_EXCEPTIONS)
    message(FATAL_ERROR "Python bindings require RTTI and exceptions to be enabled.")
endif ()

##
# A helper for creating tests with correct PYTHONPATH
##

function(add_python_test)
    cmake_parse_arguments(ARG "" "FILE;LABEL" "PYTHONPATH;ENVIRONMENT" ${ARGN})

    list(PREPEND ARG_PYTHONPATH "$<TARGET_FILE_DIR:Halide::Python>/..")
    list(TRANSFORM ARG_PYTHONPATH PREPEND "PYTHONPATH=path_list_prepend:")

    list(PREPEND ARG_ENVIRONMENT "HL_TARGET=${Halide_TARGET}")

    cmake_path(GET ARG_FILE STEM test_name)
    set(test_name "${ARG_LABEL}_${test_name}")

    add_test(
        NAME "${test_name}"
        COMMAND Python3::Interpreter "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/${ARG_FILE}>"
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

add_subdirectory(src)
add_subdirectory(stub)

if (WITH_TEST_PYTHON)
    add_subdirectory(test)
endif ()

if (WITH_TUTORIALS)
    add_subdirectory(tutorial)
endif ()
