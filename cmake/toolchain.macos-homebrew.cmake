cmake_minimum_required(VERSION 3.28)

set(CMAKE_C_COMPILER /opt/homebrew/opt/llvm@21/bin/clang)
set(CMAKE_CXX_COMPILER /opt/homebrew/opt/llvm@21/bin/clang++)

if (NOT DEFINED CMAKE_OSX_SYSROOT)
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE CMAKE_OSX_SYSROOT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
endif ()

# Let Clang order its builtin headers before the SDK's system headers.
set(CMAKE_OSX_SYSROOT "${CMAKE_OSX_SYSROOT}" CACHE PATH "")
