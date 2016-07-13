#!/bin/bash -x
set -e
set -o pipefail

# Note this script assumes that the current working directory
# is the root of the repository
if [ ! -f ./.travis.yml ]; then
  echo "This script must be run from the root of the repository"
  exit 1
fi

: ${LLVM_VERSION:?"LLVM_VERSION must be specified"}
: ${BUILD_SYSTEM:?"BUILD_SYSTEM must be specified"}
: ${CXX:?"CXX must be specified"}

if [ ${BUILD_SYSTEM} = 'CMAKE' ]; then
  : ${HALIDE_SHARED_LIBRARY:?"HALIDE_SHARED_LIBRARY must be set"}
  LLVM_VERSION_NO_DOT="$( echo ${LLVM_VERSION} | sed 's/\.//' | cut -b1,2 )"
  mkdir -p build/ && cd build/
  cmake -DLLVM_INCLUDE="/usr/local/llvm/include" \
        -DLLVM_LIB="/usr/local/llvm/lib" \
        -DLLVM_BIN="/usr/local/llvm/bin" \
        -DLLVM_VERSION="${LLVM_VERSION_NO_DOT}" \
        -DHALIDE_SHARED_LIBRARY="${HALIDE_SHARED_LIBRARY}" \
        -DWITH_APPS=ON \
        -DWITH_TESTS=ON \
        -DWITH_TEST_OPENGL=OFF \
        -DWITH_TUTORIALS=OFF \
        -DWITH_DOCS=ON \
        -DCMAKE_BUILD_TYPE=Release \
        -G "Unix Makefiles" \
        ../

  # Build and run internal tests
  make VERBOSE=1
  # Build docs
  make doc

  # Run correctness tests
  TESTCASES=$(find bin/ -iname 'correctness_*' | \
      grep -v _vector_math | \
      grep -v _vector_cast | \
      grep -v _lerp | \
      grep -v _simd_op_check | \
      grep -v _specialize_branched_loops | \
      grep -v _print | \
      grep -v _math | \
      grep -v _div_mod | \
      grep -v _fuzz_simplify | \
      grep -v _round | \
      sort)
  for TESTCASE in ${TESTCASES}; do
      echo "Running ${TESTCASE}"
      ${TESTCASE}
  done
elif [ ${BUILD_SYSTEM} = 'MAKE' ]; then
  export LLVM_CONFIG=/usr/local/llvm/bin/llvm-config
  export CLANG=/usr/local/llvm/bin/clang
  ${LLVM_CONFIG} --cxxflags --libdir --bindir

  # Build and run internal tests
  make

  # Build the docs and run the tests
  make doc test_correctness test_generators
else
  echo "Unexpected BUILD_SYSTEM: \"${BUILD_SYSTEM}\""
  exit 1
fi
