#!/bin/bash -x

set -e
set -o pipefail

: ${LLVM_VERSION:?"LLVM_VERSION must be specified"}
: ${BUILD_SYSTEM:?"BUILD_SYSTEM must be specified"}

if [ $(echo "${LLVM_VERSION}" | grep -Ec '^[0-9]+\.[0-9]$') -ne 1 ]; then
  echo "LLVM_VERSION (${LLVM_VERSION}) is not correctly formatted"
  exit 1
fi

# Set variables that the build script needs
export LLVM_INCLUDE="/usr/lib/llvm-${LLVM_VERSION}/include"
export LLVM_LIB="/usr/lib/llvm-${LLVM_VERSION}/lib"
export LLVM_BIN="/usr/lib/llvm-${LLVM_VERSION}/bin"
# Travis has 2 CPUs but only 3GiB of RAM so we need
# to avoid doing stuff in parallel to avoid the linker getting killed
export NUM_JOBS=1
export RUN_TESTS=1

# Have the generic script to the real work
test/scripts/build_generic.sh
