cmake_minimum_required(VERSION 3.22)

set(Halide_HOST_TARGET @Halide_HOST_TARGET@)

include(${CMAKE_CURRENT_LIST_DIR}/Halide-Interfaces.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/HalideTargetHelpers.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/HalideGeneratorHelpers.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/TargetExportScript.cmake)
