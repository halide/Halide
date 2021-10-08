#!/bin/bash

set -e

HANNK_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

if [ -z ${BUILD_DIR} ]; then
BUILD_DIR="${HANNK_DIR}/build"
fi
echo Using BUILD_DIR=${BUILD_DIR}

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ -z ${HALIDE_INSTALL_PATH} ]; then
HALIDE_INSTALL_PATH=${HOME}/halide-14-install/
fi
echo Using HalideInstall=${HALIDE_INSTALL_PATH}

if [ -z ${HL_TARGET} ]; then
HL_TARGET=host
fi
echo Using HL_TARGET=${HL_TARGET}

if [ -z "${CMAKE_GENERATOR}" ]; then
CMAKE_GENERATOR=Ninja
fi
echo Using build tool=${CMAKE_GENERATOR}

if [ -z "${CMAKE_BUILD_TYPE}" ]; then
CMAKE_BUILD_TYPE=Release
fi
echo Using CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}

EXTRAS=
# TODO: this doesn't work (yet); crosscompiling in CMake is painful.
if [[ "${HL_TARGET}" =~ ^arm-64-android.* ]]; then
echo Configuring for Android arm64-v8a build...
echo Using ANDROID_NDK_ROOT=${ANDROID_NDK_ROOT}
EXTRAS="-DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a"
else
echo Assuming host build...
fi

cmake \
  ${EXTRAS} \
  -G "${CMAKE_GENERATOR}" \
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
  -DHalide_DIR="${HALIDE_INSTALL_PATH}" \
  -DCMAKE_PREFIX_PATH="${HALIDE_INSTALL_PATH}" \
  -DHalide_TARGET=${HL_TARGET} \
  -S "${HANNK_DIR}" \
  -B "${BUILD_DIR}"

