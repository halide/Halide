#!/bin/bash -x
set -e
set -o pipefail

# Note this script assumes that the current working directory
# is the root of the repository
if [ ! -f ./.travis.yml ]; then
  echo "This script must be run from the root of the repository"
  exit 1
fi

: ${LLVM_INCLUDE:?"LLVM_INCLUDE must be specified"}
: ${LLVM_LIB:?"LLVM_LIB must be specified"}
: ${LLVM_BIN:?"LLVM_BIN must be specified"}
: ${LLVM_VERSION:?"LLVM_VERSION must be specified"}
: ${BUILD_SYSTEM:?"BUILD_SYSTEM must be specified"}
: ${NUM_JOBS:?"NUM_JOBS must be specified"}
: ${CXX:?"CXX must be specified"}
: ${BUILD_TYPE:?"BUILD_TYPE must be specified"}
: ${RUN_TESTS:?"RUN_TESTS must be specified"}

# By default don't do incremental build
INCREMENTAL_BUILD="${INCREMENTAL_BUILD:-0}"

if [ ${BUILD_SYSTEM} = 'CMAKE' ]; then
  : ${HALIDE_SHARED_LIBRARY:?"HALIDE_SHARED_LIBRARY must be set"}
  LLVM_VERSION_NO_DOT="$( echo ${LLVM_VERSION} | sed 's/\.//')"
  if [ ${INCREMENTAL_BUILD} = '0' ]; then
    rm -rf build/
  fi
  mkdir -p build/ && cd build/
  cmake -DLLVM_INCLUDE="${LLVM_INCLUDE}" \
        -DLLVM_LIB="${LLVM_LIB}" \
        -DLLVM_BIN="${LLVM_BIN}" \
        -DLLVM_VERSION="${LLVM_VERSION_NO_DOT}" \
        -DHALIDE_SHARED_LIBRARY="${HALIDE_SHARED_LIBRARY}" \
        -DWITH_APPS=ON \
        -DWITH_TESTS="${RUN_TESTS}" \
        -DWITH_TUTORIALS=OFF \
        -DWITH_DOCS=ON \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -G "Unix Makefiles" \
        ../

  # Build
  make -j ${NUM_JOBS}
  # Build docs
  make doc

  if [ ${RUN_TESTS} = '1' ]; then
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
  else
    echo "Not running tests"
  fi
elif [ ${BUILD_SYSTEM} = 'MAKE' ]; then
  if [ ${BUILD_TYPE} = 'Debug' ]; then
    OPT_FLAG='-O0'
  else
    OPT_FLAG='-O3'
  fi
  function make_build() {
    make LLVM_BINDIR="${LLVM_BIN}" \
         LLVM_LIBDIR="${LLVM_LIB}" \
         LLVM_CONFIG="${LLVM_BIN}/llvm-config" \
         CLANG="${LLVM_BIN}/clang" \
         CXX=${CXX} \
         OPTIMIZE="${OPT_FLAG}" \
         "$@"
  }

  if [ ${INCREMENTAL_BUILD} = '0' ]; then
    make clean
  fi
  # Build
  # Note this runs the internal tests too
  # FIXME: The CMake build system doesn't do this
  make_build -j ${NUM_JOBS}

  # Build the docs
  make_build doc

  if [ ${RUN_TESTS} = '1' ]; then
    # Build an run the correctness tests
    make_build -j ${NUM_JOBS} test_correctness
  else
    echo "Not running tests"
  fi
else
  echo "Unexpected BUILD_SYSTEM: \"${BUILD_SYSTEM}\""
  exit 1
fi
