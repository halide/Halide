# Toolchain for using Homebrew clang on macOS.
#
# Homebrew clang is badly misconfigured and needs help finding the system
# headers, even though it uses system libc++ by default. This toolchain
# queries xcrun to locate the SDK root and the Apple CLT toolchain root,
# then sets the standard include paths accordingly. The compiler's own
# resource directory (containing arm_neon.h, stdarg.h, etc.) is always in
# the built-in search path and must NOT be overridden here.

cmake_minimum_required(VERSION 3.28)

execute_process(
    COMMAND xcrun --show-sdk-path
    OUTPUT_VARIABLE _sdkroot
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

# xcrun --find clang locates the Apple CLT clang (not the Homebrew one).
# Go two directory levels up from its bin/ directory to reach the toolchain
# root (e.g. /Library/Developer/CommandLineTools).
# NOTE: eventually this can be replaced by `xcrun --show-toolchain-path`
execute_process(
    COMMAND xcrun --find clang
    OUTPUT_VARIABLE _clang_path
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
cmake_path(GET _clang_path PARENT_PATH _clang_bin_dir)
cmake_path(GET _clang_bin_dir PARENT_PATH _clang_usr_dir)
cmake_path(GET _clang_usr_dir PARENT_PATH _toolchainroot)

set(CMAKE_SYSROOT "${_sdkroot}")
set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES
    "${_sdkroot}/usr/include"
    "${_toolchainroot}/usr/include"
    "${_sdkroot}/System/Library/Frameworks"
    "${_sdkroot}/System/Library/SubFrameworks"
)

# Homebrew clang's built-in include path finds its own libc++ headers, which
# have ABI tags incompatible with the system libc++ we link against. Use
# -nostdinc++ to suppress the built-in C++ search path so only the system
# libc++ headers (provided below) are used.
string(APPEND CMAKE_CXX_FLAGS_INIT " -nostdinc++")
set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES
    "${_sdkroot}/usr/include/c++/v1"
    ${CMAKE_C_STANDARD_INCLUDE_DIRECTORIES}
)
