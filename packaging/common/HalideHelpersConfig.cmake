cmake_minimum_required(VERSION 3.16)

set(Halide_HOST_TARGET @Halide_HOST_TARGET@)

include(CMakeFindDependencyMacro)

set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_dependency(Threads)

include(${CMAKE_CURRENT_LIST_DIR}/Halide-Interfaces.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/HalideTargetHelpers.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/HalideGeneratorHelpers.cmake)
