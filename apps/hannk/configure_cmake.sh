#!/bin/bash

set -e

HANNK_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

if [ -z ${BUILD_DIR} ]; then
BUILD_DIR="${HANNK_DIR}/build"
fi

if [ -z ${HALIDE_INSTALL_PATH} ]; then
HALIDE_INSTALL_PATH=${HOME}/halide-14-install/
fi

if [ -z ${HL_TARGET} ]; then
HL_TARGET=host
fi

if [ -z "${CMAKE_GENERATOR}" ]; then
CMAKE_GENERATOR=Ninja
fi

if [ -z "${CMAKE_BUILD_TYPE}" ]; then
CMAKE_BUILD_TYPE=Release
fi

if [ -z "${HANNK_BUILD_TFLITE}" ]; then
HANNK_BUILD_TFLITE=ON
else
HANNK_BUILD_TFLITE=OFF
fi

PREFIX=
EXTRAS=

if [[ "${HL_TARGET}" =~ ^arm-64-android.* ]]; then

  # TODO: this doesn't work (yet); crosscompiling in CMake is painful.
  echo Configuring for Android arm64-v8a build...
  echo Using ANDROID_NDK_ROOT=${ANDROID_NDK_ROOT}
  echo Using CMAKE_TOOLCHAIN_FILE=${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake

  EXTRAS="-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE} -DANDROID_ABI=arm64-v8a"

elif [[ "${HL_TARGET}" =~ ^wasm-32-wasmrt.* ]]; then

    PREFIX=emcmake
    HANNK_BUILD_TFLITE=OFF

else

  echo Assuming host build...

fi

if [ -n "${NODE_JS_EXECUTABLE}" ]; then
EXTRAS="${EXTRAS} -DNODE_JS_EXECUTABLE=${NODE_JS_EXECUTABLE}"
echo Using NODE_JS_EXECUTABLE=${NODE_JS_EXECUTABLE}
fi

echo Using HalideInstall=${HALIDE_INSTALL_PATH}
echo Using BUILD_DIR=${BUILD_DIR}
echo Using build tool=${CMAKE_GENERATOR}
echo Using CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
echo Using HL_TARGET=${HL_TARGET}
echo Using HANNK_BUILD_TFLITE=${HANNK_BUILD_TFLITE}

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

${PREFIX} cmake \
  ${EXTRAS} \
  -G "${CMAKE_GENERATOR}" \
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
  -DHalide_DIR="${HALIDE_INSTALL_PATH}/lib/cmake/Halide" \
  -DHalideHelpers_DIR="${HALIDE_INSTALL_PATH}/lib/cmake/HalideHelpers" \
  -DHalide_TARGET=${HL_TARGET} \
  -DHANNK_BUILD_TFLITE=${HANNK_BUILD_TFLITE} \
  -S "${HANNK_DIR}" \
  -B "${BUILD_DIR}"
