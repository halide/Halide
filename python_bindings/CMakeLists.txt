##
# Load Python dependencies, including external pybind11
##

find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

set(PYBIND11_MIN_SUPPORTED_VER 2.6.2)
find_package(pybind11 ${PYBIND11_MIN_SUPPORTED_VER} QUIET)
if (NOT pybind11_FOUND)
    set(PYBIND11_BUNDLED_VER 2.9.0)
    include(FetchContent)
    FetchContent_Declare(pybind11
                         GIT_REPOSITORY https://github.com/pybind/pybind11.git
                         GIT_TAG v${PYBIND11_BUNDLED_VER})
    FetchContent_MakeAvailable(pybind11)
endif ()

##
# Add our sources to this sub-tree.
##

add_subdirectory(src)
include(stub/CMakeLists.txt)

option(WITH_TEST_PYTHON "Build Python tests" ON)
if (WITH_TESTS AND WITH_TEST_PYTHON)
    add_subdirectory(apps)
    add_subdirectory(correctness)
    add_subdirectory(tutorial)
endif ()
