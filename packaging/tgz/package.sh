#!/bin/bash
set -e -o pipefail

halide_source=$(realpath "$1")
halide_build_root=$(realpath "$2")

[ -z "$LLVM_DIR" ] && echo "Must set specific LLVM_DIR for packaging" && exit
[ -z "$Clang_DIR" ] && echo "Must set specific Clang_DIR for packaging" && exit
[ -z "$halide_source" ] && echo "Usage: $0 <source-dir> <build-dir>" && exit
[ -z "$halide_build_root" ] && echo "Usage: $0 <source-dir> <build-dir>" && exit

cmake --preset=package-unix-shared -S "$halide_source" -B "$halide_build_root/shared-Release" \
  "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
cmake --preset=package-unix-static -S "$halide_source" -B "$halide_build_root/static-Release" \
  "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"

cmake --build "$halide_build_root/shared-Release"
cmake --build "$halide_build_root/static-Release"

cd "$halide_build_root"

cpack -G TGZ -C Release --config "$halide_source/packaging/tgz/config.cmake"
