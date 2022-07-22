##
# Load Python dependencies, including external pybind11
##

find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

set(PYBIND11_VER 2.6.2)
find_package(pybind11 ${PYBIND11_VER} QUIET)
if (NOT pybind11_FOUND)
    include(FetchContent)
    FetchContent_Declare(pybind11
                         GIT_REPOSITORY https://github.com/pybind/pybind11.git
                         GIT_TAG v${PYBIND11_VER})
    FetchContent_MakeAvailable(pybind11)
endif ()

##
# Add our sources to this sub-tree.
##

add_subdirectory(src/halide)
add_subdirectory(stub)

option(WITH_TEST_PYTHON "Build Python tests" ON)
if (WITH_TESTS AND WITH_TEST_PYTHON)
    if (Halide_ENABLE_ASAN)
        # Getting asan static-vs-shared library settings, LD_PRELOAD, etc.
        # is necessary for asan stuff to work properly in loaded Python extensions;
        # that isn't done yet, so just skip these for now.
        # TODO(srj): add a tracking issue here, we should be able to enable these.
        message(STATUS "Python tests are not yet supported under ASAN.")
    else()
        add_subdirectory(test)
        add_subdirectory(tutorial)
    endif()
endif ()

