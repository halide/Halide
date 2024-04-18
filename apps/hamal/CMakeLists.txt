cmake_minimum_required(VERSION 3.22)
project(hamal)

# We need to set this for some of the subprojects pulled in by TFLite (eg flatbuffers)
# set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

enable_testing()

# ----------------------------

# Compatibility cruft
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
  cmake_policy(SET CMP0135 NEW)
endif()

# ----------------------------

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# ----------------------------

# Find HalideHelpers -- this is just the Runtime headers and CMake functions, but no libraries
find_package(Halide REQUIRED)

# ----------------------------

# go get googletest and googlebenchmark

include(FetchContent)

set(ABSL_PROPAGATE_CXX_STD ON)

FetchContent_Declare(abseil
                     GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
                     GIT_TAG 20240116.2)
FetchContent_Declare(googletest
                     GIT_REPOSITORY https://github.com/google/googletest.git
                     GIT_TAG v1.14.0)
FetchContent_Declare(googlebenchmark
                     GIT_REPOSITORY https://github.com/google/benchmark.git
                     GIT_TAG v1.8.3)

FetchContent_MakeAvailable(abseil googletest googlebenchmark)

# ----------------------------

add_subdirectory(src)
add_subdirectory(test)

# ----------------------------
