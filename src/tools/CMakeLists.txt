cmake_minimum_required(VERSION 3.16)
project(host-tools)

# Require standard C++14
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(binary2cpp binary2cpp.cpp)
add_executable(build_halide_h build_halide_h.cpp)
add_executable(find_inverse find_inverse.cpp)

# Don't install when included via add_subdirectory.
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(GNUInstallDirs)
    install(TARGETS build_halide_h binary2cpp find_inverse
            EXPORT host-tools
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif ()
