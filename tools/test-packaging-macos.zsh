#!/bin/zsh

DIR="$(cd "$(dirname "${(%):-%N}")"/.. >/dev/null 2>&1 && pwd)"

[ -z "$LLVM_DIR" ] && echo "Must set specific LLVM_DIR for packaging" && exit
[ -z "$Clang_DIR" ] && echo "Must set specific Clang_DIR for packaging" && exit

FLAGS="-DWITH_TESTS=NO -DWITH_TUTORIALS=NO -DWITH_PYTHON_BINDINGS=NO -DWITH_APPS=YES -DWITH_DOCS=NO -DWITH_UTILS=NO -DLLVM_DIR=$LLVM_DIR -DClang_DIR=$Clang_DIR"

Halide_static="-DBUILD_SHARED_LIBS=NO"
Halide_shared="-DBUILD_SHARED_LIBS=YES"

LLVM_static=""
LLVM_bundled="-DHalide_BUNDLE_LLVM=YES"
LLVM_shared="-DHalide_SHARED_LLVM=YES"

for HL in static shared; do
  for LLVM in static bundled shared; do
    if [[ "$HL|$LLVM" == "shared|bundled" ]]; then
      continue
    fi

    Halide_flags_var=Halide_$HL
    LLVM_flags_var=LLVM_$LLVM
    build_dir="$DIR/build/release-$HL-$LLVM"

    echo HL=$HL LLVM=$LLVM
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ${=FLAGS} ${(P)Halide_flags_var} ${(P)LLVM_flags_var} -S "$DIR" -B ${build_dir}
    cmake --build ${build_dir} && (cd ${build_dir} && ctest -R bgu)
    echo
    echo
  done
done
