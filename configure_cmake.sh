#!/bin/bash

# Create a plausible default CMake build setup for Halide.
# You can selectively override defaults via env vars, e.g. to configure a Debug
# build but otherwise accept all defaults:
#
#   CMAKE_BUILD_TYPE=Debug ./configure_cmake.sh
#
# or, to reconfigure HL_TARGET to enable OpenCL:
#
#   HL_TARGET=host-opencl ./configure_cmake.sh
#
# etc.
#
# You can also pass --clean to remove the existing build dir (if any).

set -e

HALIDE_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
BUILD_DIR="${HALIDE_DIR}/build"

if [ "$1" != "" ]; then
  if [ "$1" != "--clean" ]; then
    echo "The only supported argument is --clean"
    exit
  fi
  echo "*** REMOVING *** ${BUILD_DIR} ..."
  rm -rf "${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ -z ${LLVM_VERSION} ]; then
LLVM_VERSION=14
fi
echo "Using LLVM_VERSION=${LLVM_VERSION}"

if [ -z ${LLVM_DIR} ]; then
LLVM_DIR="${HOME}/llvm-${LLVM_VERSION}-install/lib/cmake/llvm"
fi
echo "Using LLVM_DIR=${LLVM_DIR}"

if [ -z ${HALIDE_VERSION} ]; then
HALIDE_VERSION=${LLVM_VERSION}
fi
echo "Using HALIDE_VERSION=${HALIDE_VERSION}"

if [ -z ${HALIDE_INSTALL_PATH} ]; then
HALIDE_INSTALL_PATH=${HOME}/halide-${HALIDE_VERSION}-install/
fi
echo "Using HALIDE_INSTALL_PATH=${HALIDE_INSTALL_PATH}"

if [ -z ${HL_TARGET} ]; then
HL_TARGET=host
fi
echo "Using HL_TARGET=${HL_TARGET}"

if [ -z "${CMAKE_GENERATOR}" ]; then
CMAKE_GENERATOR=Ninja
fi
echo "Using CMAKE_GENERATOR=${CMAKE_GENERATOR}"

if [ -z "${CMAKE_BUILD_TYPE}" ]; then
CMAKE_BUILD_TYPE=Release
fi
echo "Using CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"

if [ -z "${Halide_CCACHE_BUILD}" ]; then
Halide_CCACHE_BUILD=ON
fi
echo "Using Halide_CCACHE_BUILD=${Halide_CCACHE_BUILD}"

echo
echo "Configuring CMake...."
cmake \
  -G "${CMAKE_GENERATOR}" \
  -D CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
  -D CMAKE_PREFIX_PATH="${HALIDE_INSTALL_PATH}" \
  -D Halide_CCACHE_BUILD="${Halide_CCACHE_BUILD}" \
  -D Halide_DIR="${HALIDE_INSTALL_PATH}" \
  -D Halide_TARGET=${HL_TARGET} \
  -D LLVM_DIR="${LLVM_DIR}" \
  -S "${HALIDE_DIR}" \
  -B "${BUILD_DIR}"

echo
echo "CMake build dir is ${BUILD_DIR}"
