##
# Load Python dependencies, including external pybind11
##

find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

# Adding PyBind11 to the build is the recommended way of integrating with CMake.
# See: https://pybind11.readthedocs.io/en/stable/compiling.html

# Configure pybind11 to use the same interpreter version as was detected above.
message(STATUS "Directing pybind11 to Python3 executable ${Python3_EXECUTABLE}")
set(PYTHON_EXECUTABLE ${Python3_EXECUTABLE})

# Keep the version in sync with requirements.txt and the Ubuntu 20.04 LTS package (python3-pybind11)
set(PYBIND11_VER 2.5.0)
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

add_subdirectory(src)
include(stub/CMakeLists.txt)

option(WITH_TEST_PYTHON "Build Python tests" ON)
if (WITH_TESTS AND WITH_TEST_PYTHON)
    add_subdirectory(apps)
    add_subdirectory(correctness)
    add_subdirectory(tutorial)
endif ()
