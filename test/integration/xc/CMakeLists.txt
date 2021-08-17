cmake_minimum_required(VERSION 3.16)
project(xc)

enable_testing()

if (CMAKE_CROSSCOMPILING)
    find_package(xc-generators REQUIRED)
else ()
    add_subdirectory(generators)
endif ()

add_subdirectory(add)
