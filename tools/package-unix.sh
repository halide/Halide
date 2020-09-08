#!/bin/bash

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. >/dev/null 2>&1 && pwd)"

[ -z "$LLVM_DIR" ] && echo "Must set specific LLVM_DIR for packaging" && exit
[ -z "$Clang_DIR" ] && echo "Must set specific Clang_DIR for packaging" && exit

cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=NO -DHalide_BUNDLE_LLVM=YES \
  -DLLVM_DIR="$LLVM_DIR" -DClang_DIR="$Clang_DIR" \
  -DWITH_TESTS=NO -DWITH_APPS=NO -DWITH_TUTORIALS=NO \
  -DWITH_DOCS=YES -DWITH_UTILS=NO -DWITH_PYTHON_BINDINGS=NO \
  -S "$DIR" -B "$DIR/build/static-Release"

cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=YES \
  -DLLVM_DIR="$LLVM_DIR" -DClang_DIR="$Clang_DIR" \
  -DWITH_TESTS=NO -DWITH_APPS=NO -DWITH_TUTORIALS=NO \
  -DWITH_DOCS=YES -DWITH_UTILS=NO -DWITH_PYTHON_BINDINGS=NO \
  -S "$DIR" -B "$DIR/build/shared-Release"

cmake --build "$DIR/build/static-Release"
cmake --build "$DIR/build/shared-Release"

(
  cd "$DIR/build" || exit
  cpack --config "$DIR/packaging/Unix.cmake"
)
