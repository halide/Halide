#!/bin/bash

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. >/dev/null 2>&1 && pwd)"

FLAGS="-DWITH_TESTS=NO -DWITH_TUTORIALS=NO -DWITH_PYTHON_BINDINGS=NO -DWITH_APPS=YES -DWITH_DOCS=NO -DWITH_UTILS=NO"

Halide_static="-DBUILD_SHARED_LIBS=NO"
Halide_shared="-DBUILD_SHARED_LIBS=YES"

LLVM_static=""
LLVM_bundled="-DHalide_BUNDLE_LLVM=YES"
LLVM_shared="-DHalide_SHARED_LLVM=YES"

for HL in static shared; do
  for LLVM in static bundled shared; do
    if [ "$HL|$LLVM" == "shared|bundled" ]; then
      continue
    fi

    Halide_flags_var=Halide_$HL
    LLVM_flags_var=LLVM_$LLVM
    build_dir="$DIR/build/release-$HL-$LLVM"

    echo HL=$HL LLVM=$LLVM
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ${FLAGS} ${!Halide_flags_var} ${!LLVM_flags_var} -S "$DIR" -B "$build_dir"
    cmake --build ${build_dir} && (cd ${build_dir} && ctest -R bgu)
    echo
    echo
  done
done
