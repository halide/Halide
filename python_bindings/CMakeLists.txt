# Ensure RTTI and exceptions are enabled
if (NOT Halide_ENABLE_RTTI OR NOT Halide_ENABLE_EXCEPTIONS)
    message(WARNING "Building Python bindings disabled: "
            "must compile with exceptions [${Halide_ENABLE_EXCEPTIONS}] AND RTTI [${Halide_ENABLE_RTTI}]")
    return()
endif ()

# Load Python dependencies, including external pybind11
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
find_package(pybind11 2.6.2 REQUIRED)

# Add our sources to this sub-tree.
add_subdirectory(src)
include(stub/CMakeLists.txt)

option(WITH_TEST_PYTHON "Build Python tests" ON)
if (WITH_TESTS AND WITH_TEST_PYTHON)
    add_subdirectory(apps)
    add_subdirectory(correctness)
    add_subdirectory(tutorial)
endif ()
