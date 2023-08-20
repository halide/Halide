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

# Patch busted flatbuffers build that doesn't play nice with FetchContent
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