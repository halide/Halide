#!/bin/bash

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. >/dev/null 2>&1 && pwd)"

[ -z "$LLVM_DIR" ] && echo "Must set specific LLVM_DIR for packaging" && exit

# shellcheck disable=SC2154
[ -z "$Clang_DIR" ] && echo "Must set specific Clang_DIR for packaging" && exit

# shellcheck disable=SC2034
flag_shared="-DBUILD_SHARED_LIBS=YES"

# shellcheck disable=SC2034
flag_static="-DBUILD_SHARED_LIBS=NO"

for ty in shared static; do
  for cfg in Debug Release; do
    flag_name=flag_$ty
    cmake -G Ninja -DCMAKE_BUILD_TYPE=$cfg ${!flag_name} \
      -DLLVM_DIR="$LLVM_DIR" -DClang_DIR="$Clang_DIR" \
      -DWITH_TESTS=NO -DWITH_APPS=NO -DWITH_TUTORIALS=NO \
      -DWITH_DOCS=YES -DWITH_UTILS=NO -DWITH_PYTHON_BINDINGS=NO \
      -S "$DIR" -B "$DIR/build/$ty-$cfg"
    cmake --build "$DIR/build/$ty-$cfg"
  done
done

(
  cd "$DIR/build" || exit
  cpack --config "$DIR/packaging/Linux.cmake"
)
