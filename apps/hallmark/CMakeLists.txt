cmake_minimum_required(VERSION 3.22)
project(hallmark)

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
find_package(ZLIB REQUIRED)

# ----------------------------

# go get various deps we need

include(FetchContent)

set(ABSL_PROPAGATE_CXX_STD ON)
set(ABSL_USE_EXTERNAL_GOOGLETEST ON)
FetchContent_Declare(abseil
                     GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
                     GIT_TAG 20240116.2)

set(FLATBUFFERS_BUILD_TESTS OFF)
set(FLATBUFFERS_INSTALL OFF)
FetchContent_Declare(flatbuffers
                     GIT_REPOSITORY https://github.com/google/flatbuffers.git
                     GIT_TAG v23.5.26
                     GIT_SHALLOW TRUE)

set(BUILD_GMOCK OFF)
set(INSTALL_GTEST OFF)
set(GTEST_HAS_ABSL OFF)
FetchContent_Declare(googletest
                     GIT_REPOSITORY https://github.com/google/googletest.git
                     GIT_TAG v1.14.0)

set(BENCHMARK_ENABLE_TESTING OFF)
set(BENCHMARK_ENABLE_EXCEPTIONS OFF)
set(BENCHMARK_ENABLE_INSTALL OFF)
set(BENCHMARK_INSTALL_DOCS OFF)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF)
FetchContent_Declare(googlebenchmark
                     GIT_REPOSITORY https://github.com/google/benchmark.git
                     GIT_TAG v1.8.3)

set(protobuf_INSTALL OFF)
set(protobuf_BUILD_TESTS OFF)
set(protobuf_BUILD_CONFORMANCE OFF)
set(protobuf_BUILD_EXAMPLES OFF)
set(protobuf_BUILD_PROTOC_BINARIES ON)
set(protobuf_BUILD_LIBPROTOC OFF)
set(protobuf_BUILD_LIBUPB OFF)
set(protobuf_DISABLE_RTTI ON)
set(protobuf_WITH_ZLIB ON CACHE BOOL "" FORCE)
FetchContent_Declare(protobuf
                     GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
                     GIT_TAG v26.1
                     GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(abseil flatbuffers googletest googlebenchmark protobuf)

# ---------- Set up targets for flatc
add_library(hallmark_flatbuffers INTERFACE)
target_sources(hallmark_flatbuffers INTERFACE $<BUILD_INTERFACE:$<TARGET_OBJECTS:flatbuffers>>)
target_include_directories(hallmark_flatbuffers
                           SYSTEM # Use -isystem instead of -I; this is a trick so that clang-tidy won't analyze these includes
                           INTERFACE
                           $<BUILD_INTERFACE:${flatbuffers_SOURCE_DIR}>/include
                           $<BUILD_INTERFACE:${flatbuffers_BINARY_DIR}>/include)
set_target_properties(hallmark_flatbuffers PROPERTIES EXPORT_NAME flatbuffers)
add_executable(flatbuffers::flatc ALIAS flatc)

# ---------- Set up targets for protobuf
FetchContent_GetProperties(protobuf SOURCE_DIR protobuf_SOURCE_DIR)
# Include the script which defines 'protobuf_generate'
include(${protobuf_SOURCE_DIR}/cmake/protobuf-generate.cmake)

# ----------------------------

add_subdirectory(contrib)
add_subdirectory(src)
add_subdirectory(test)

# ----------------------------
