cmake_minimum_required(VERSION 3.24)

include(FetchContent)

##
# Flatbuffers

FetchContent_Declare(
    flatbuffers
    GIT_REPOSITORY https://github.com/google/flatbuffers.git
    GIT_TAG 0100f6a5779831fa7a651e4b67ef389a8752bd9b # v23.5.26
    GIT_SHALLOW TRUE
    OVERRIDE_FIND_PACKAGE
)

# Fix up the targets
if (NOT EXISTS "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/flatbuffers-extra.cmake")
    file(WRITE "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/flatbuffers-extra.cmake" [=[
add_library(flatbuffers::flatbuffers ALIAS flatbuffers)
if (BUILD_SHARED_LIBS)
    set_property(TARGET flatbuffers PROPERTY POSITION_INDEPENDENT_CODE ON)
endif ()
]=])
endif ()

##
# pybind11

FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG 5b0a6fc2017fcc176545afe3e09c9f9885283242 # v3.10.4
    GIT_SHALLOW TRUE
    OVERRIDE_FIND_PACKAGE
)

##
# wabt

FetchContent_Declare(
    wabt
    GIT_REPOSITORY https://github.com/WebAssembly/wabt.git
    GIT_TAG 963f973469b45969ce198e0c86d3af316790a780 # 1.0.33
    GIT_SHALLOW TRUE
    OVERRIDE_FIND_PACKAGE
)

# Configure the project for vendored usage
set(CMAKE_PROJECT_WABT_INCLUDE "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/wabt-vars.cmake")
if (NOT EXISTS "${CMAKE_PROJECT_WABT_INCLUDE}")
    file(WRITE "${CMAKE_PROJECT_WABT_INCLUDE}" [=[
set(WITH_EXCEPTIONS ${Halide_ENABLE_EXCEPTIONS})
set(BUILD_TESTS OFF)
set(BUILD_TOOLS OFF)
set(BUILD_LIBWASM OFF)
set(USE_INTERNAL_SHA256 ON)
]=])
endif ()

# Fix up the targets
if (NOT EXISTS "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/wabt-extra.cmake")
    file(WRITE "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/wabt-extra.cmake" [=[
target_compile_options(
    wabt PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-alloca-larger-than>
)
if (BUILD_SHARED_LIBS)
    set_property(TARGET wabt PROPERTY POSITION_INDEPENDENT_CODE ON)
endif ()
]=])
endif ()
