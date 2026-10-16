cmake_minimum_required(VERSION 3.28)

set(CMAKE_C_COMPILER /opt/homebrew/opt/llvm@21/bin/clang)
set(CMAKE_CXX_COMPILER /opt/homebrew/opt/llvm@21/bin/clang++)

# Let Clang order its builtin headers before the SDK's system headers.
set(CMAKE_OSX_SYSROOT macosx CACHE STRING "macOS SDK")
