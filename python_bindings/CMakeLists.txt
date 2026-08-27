cmake_minimum_required(VERSION 3.28)
project(Halide_Python VERSION 22.0.0)

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

# Duplicated options from parent project
option(WITH_TESTS "Build tests" "${PROJECT_IS_TOP_LEVEL}")
option(WITH_TUTORIALS "Build tutorials" "${PROJECT_IS_TOP_LEVEL}")
option(WITH_PACKAGING "Include install() rules" "${PROJECT_IS_TOP_LEVEL}")

# Support not actually building the bindings, but using the ones we find
# via `find_package(Halide)`. This allows running tests against the
# installed Halide package.
option(WITH_PYTHON_BINDINGS "Build Python bindings" ON)

cmake_dependent_option(WITH_PYTHON_STUBS "Build Python stubs" ON WITH_PYTHON_BINDINGS OFF)

cmake_dependent_option(WITH_TEST_PYTHON "Build Python tests" ON WITH_TESTS OFF)

##
# Dependencies
##

# The plain Development component is the same as requesting both
# Development.Module and Development.Embed. We don't need the Embed
# part, so only requesting Module avoids failures when Embed is not
# available, as is the case in the manylinux Docker images.
find_package(Python 3.10 REQUIRED Interpreter Development.Module)

if (WITH_PYTHON_BINDINGS)
    find_package(pybind11 2.11.1 REQUIRED)
endif ()

# Note: this must happen, especially when WITH_PYTHON_BINDINGS is OFF.
find_package(Halide REQUIRED)
if (NOT Halide_ENABLE_RTTI OR NOT Halide_ENABLE_EXCEPTIONS)
    message(FATAL_ERROR "Python bindings require RTTI and exceptions to be enabled.")
endif ()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/halide/cmake")
include(AddPythonTest)

##
# Add our sources to this sub-tree.
##

# Package subtrees are deliberately ordered: the binary package owns the
# compiler installation, the runtime owns the header-only extension, and the
# compiler package consumes both while adding the familiar Python surface.
set(Halide_Python_PACKAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/site-packages/halide")
add_subdirectory(halide-bin)
if (WITH_PYTHON_BINDINGS)
    add_subdirectory(halide-runtime)
endif ()
add_subdirectory(halide)
