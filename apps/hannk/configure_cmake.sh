#!/bin/bash

set -e

HANNK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

if [ -z "${BUILD_DIR}" ]; then
  BUILD_DIR="${HANNK_DIR}/build"
fi

if [ -z "${HALIDE_INSTALL_PATH}" ]; then
  HALIDE_INSTALL_PATH="${HOME}/halide-14-install/"
fi

if [ -z "${HL_TARGET}" ]; then
  HL_TARGET=host
fi

if [ -z "${CMAKE_GENERATOR}" ]; then
  CMAKE_GENERATOR=Ninja
fi

if [ -z "${CMAKE_BUILD_TYPE}" ]; then
  CMAKE_BUILD_TYPE=Release
fi

if [ -z "${ANDROID_PLATFORM}" ]; then
  ANDROID_PLATFORM=21
fi

if [ -z "${HANNK_BUILD_TFLITE}" ]; then
  HANNK_BUILD_TFLITE=ON
else
  HANNK_BUILD_TFLITE=OFF
fi

## In a cross-compiling scenario, use a separate host and build dir
# TODO: figure out if there's a way to generalize "is this crosscompiling or not", and just make this a single if-else

if [[ "${HL_TARGET}" =~ ^arm-64-android.* ]]; then
  HOST_BUILD_DIR="${BUILD_DIR}/_host"
  HOST_BUILD_TARGET=(--target hannk-halide_generators)
  HOST_HL_TARGET=host
  HOST_CMAKE_DEFS=(-DHANNK_AOT_HOST_ONLY=ON)
elif [[ "${HL_TARGET}" =~ ^wasm-32-wasmrt.* ]]; then
  HOST_BUILD_DIR="${BUILD_DIR}/_host"
  HOST_BUILD_TARGET=(--target hannk-halide_generators)
  HOST_HL_TARGET=host
  HOST_CMAKE_DEFS=(-DHANNK_AOT_HOST_ONLY=ON)
else
  HOST_BUILD_DIR="${BUILD_DIR}"
  HOST_BUILD_TARGET=()
  HOST_HL_TARGET="${HL_TARGET}"
  HOST_CMAKE_DEFS=()
fi

## Build HANNK for the host no matter what

echo "Configuring HANNK for ${HOST_HL_TARGET}"
cmake \
  -G "${CMAKE_GENERATOR}" \
  -S "${HANNK_DIR}" \
  -B "${HOST_BUILD_DIR}" \
  "${HOST_CMAKE_DEFS[@]}" \
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
  -DHalide_DIR="${HALIDE_INSTALL_PATH}/lib/cmake/Halide" \
  -DHalideHelpers_DIR="${HALIDE_INSTALL_PATH}/lib/cmake/HalideHelpers" \
  -DHalide_TARGET="${HOST_HL_TARGET}" \
  -DHANNK_BUILD_TFLITE=${HANNK_BUILD_TFLITE}

if [ -z "${HOST_BUILD_TARGET[*]}" ]; then
  echo "Building HANNK for ${HOST_HL_TARGET}"
else
  echo "Building HANNK host generator executables"
fi
cmake --build "${HOST_BUILD_DIR}" "${HOST_BUILD_TARGET[@]}"

## Now if we're cross-compiling for Android or WASM, set up the build
## for that, using the platform-provided CMake toolchain files.

if [[ "${HL_TARGET}" =~ ^arm-64-android.* ]]; then
  echo "Using ANDROID_NDK_ROOT=${ANDROID_NDK_ROOT}"
  echo "Using CMAKE_TOOLCHAIN_FILE=${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake"
  CROSS_CMAKE_DEFS=(
    -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake"
    -DANDROID_ABI=arm64-v8a
    "-DANDROID_PLATFORM=${ANDROID_PLATFORM}"
    # Required because TFLite's internal Eigen tries to compile an unnecessary BLAS with the system Fortran compiler.
    "-DCMAKE_Fortran_COMPILER=NO"
  )
elif [[ "${HL_TARGET}" =~ ^wasm-32-wasmrt.* ]]; then
  echo "Using NODE_JS_EXECUTABLE=${NODE_JS_EXECUTABLE}"
  CROSS_CMAKE_DEFS=(
    -DCMAKE_TOOLCHAIN_FILE="${EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
    -DNODE_JS_EXECUTABLE="${NODE_JS_EXECUTABLE}"
  )
else
  # Not cross-compiling, so we're done.
  exit
fi

echo "Configuring cross-build HANNK for ${HL_TARGET}"
cmake \
  -G "${CMAKE_GENERATOR}" \
  -S "${HANNK_DIR}" \
  -B "${BUILD_DIR}" \
  "${CROSS_CMAKE_DEFS[@]}" \
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
  -DHANNK_BUILD_TFLITE=OFF \
  -DHalide_TARGET="${HL_TARGET}" \
  -DHalideHelpers_DIR="${HALIDE_INSTALL_PATH}/lib/cmake/HalideHelpers" \
  -Dhannk-halide_generators_ROOT="${HOST_BUILD_DIR}"

echo "Building cross-build HANNK for ${HL_TARGET}"
cmake --build "${BUILD_DIR}"
