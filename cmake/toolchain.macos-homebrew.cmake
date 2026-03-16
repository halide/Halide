cmake_minimum_required(VERSION 3.28)

set(CMAKE_C_COMPILER /opt/homebrew/opt/llvm@21/bin/clang)
set(CMAKE_CXX_COMPILER /opt/homebrew/opt/llvm@21/bin/clang++)

if (NOT DEFINED CMAKE_SYSROOT)
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE CMAKE_SYSROOT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
endif ()

set(CMAKE_SYSROOT "${CMAKE_SYSROOT}" CACHE PATH "")

if (NOT DEFINED XC_TOOLCHAIN_PATH)
    execute_process(
        COMMAND xcrun --show-toolchain-path
        OUTPUT_VARIABLE XC_TOOLCHAIN_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
endif ()

set(XC_TOOLCHAIN_PATH "${XC_TOOLCHAIN_PATH}" CACHE PATH "")

set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES
    "${CMAKE_SYSROOT}/usr/include"
    "${XC_TOOLCHAIN_PATH}/usr/include"
    "${CMAKE_SYSROOT}/System/Library/Frameworks"
    "${CMAKE_SYSROOT}/System/Library/SubFrameworks"
)

set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES
    "${CMAKE_SYSROOT}/usr/include/c++/v1"
    ${CMAKE_C_STANDARD_INCLUDE_DIRECTORIES}
)
