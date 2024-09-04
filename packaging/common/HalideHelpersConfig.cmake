cmake_minimum_required(VERSION 3.28)
@PACKAGE_INIT@

set(Halide_HOST_TARGET @Halide_HOST_TARGET@)

include(${CMAKE_CURRENT_LIST_DIR}/Halide-Interfaces.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/HalideTargetHelpers.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/HalideGeneratorHelpers.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/TargetExportScript.cmake)

check_required_components(${CMAKE_FIND_PACKAGE_NAME})
