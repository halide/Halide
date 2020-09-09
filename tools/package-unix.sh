#!/bin/bash

halide_source="$1"
halide_build_root="$2"

[ -z "$LLVM_DIR" ] && echo "Must set specific LLVM_DIR for packaging" && exit
[ -z "$Clang_DIR" ] && echo "Must set specific Clang_DIR for packaging" && exit
[ -z "$halide_source" ] && echo "Usage: $0 <source-dir> <build-dir>" && exit
[ -z "$halide_build_root" ] && echo "Usage: $0 <source-dir> <build-dir>" && exit

cmake -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=YES \
  -DLLVM_DIR="$LLVM_DIR" \
  -DClang_DIR="$Clang_DIR" \
  -DWITH_TESTS=NO \
  -DWITH_APPS=NO \
  -DWITH_TUTORIALS=NO \
  -DWITH_DOCS=YES \
  -DWITH_UTILS=NO \
  -DWITH_PYTHON_BINDINGS=NO \
  -S "$halide_source" \
  -B "$halide_build_root/shared-Release"

cmake -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=NO \
  -DHalide_BUNDLE_LLVM=YES \
  -DLLVM_DIR="$LLVM_DIR" \
  -DClang_DIR="$Clang_DIR" \
  -DWITH_TESTS=NO \
  -DWITH_APPS=NO \
  -DWITH_TUTORIALS=NO \
  -DWITH_DOCS=YES \
  -DWITH_UTILS=NO \
  -DWITH_PYTHON_BINDINGS=NO \
  -S "$halide_source" \
  -B "$halide_build_root/static-Release"

cmake --build "$halide_build_root/shared-Release"
cmake --build "$halide_build_root/static-Release"

cd "$halide_build_root" || exit
cat <<EOM >release.cmake
include("shared-Release/CPackConfig.cmake")

set(CPACK_COMPONENTS_HALIDE_RUNTIME Halide_Runtime)
set(CPACK_COMPONENTS_HALIDE_DEVELOPMENT Halide_Development)

set(CPACK_INSTALL_CMAKE_PROJECTS
    # We don't package debug binaries on Unix systems. Our developers
    # don't use them and LLVM in debug mode is next to unusable, too.
    # static-Debug Halide ALL /
    # shared-Debug Halide ALL /
    static-Release Halide ALL /
    shared-Release Halide ALL /
    )
EOM

cpack --config release.cmake
