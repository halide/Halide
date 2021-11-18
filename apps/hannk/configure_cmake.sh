#!/bin/bash

set -e

die() {
  echo "$@"
  exit 1
}

HANNK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

# Set variable default values
: "${BUILD_DIR:=${HANNK_DIR}/build}"
: "${HALIDE_INSTALL_PATH:=${HOME}/halide-14-install}"
: "${HL_TARGET:=host}"
: "${CMAKE_GENERATOR:=Ninja}"
: "${CMAKE_BUILD_TYPE:=Release}"

# Validate HALIDE_INSTALL_PATH (too brittle without the check)
[ -d "${HALIDE_INSTALL_PATH}" ] || die "HALIDE_INSTALL_PATH must point to a directory"

# Default options for host-only build.
SOURCE_DIR="${HANNK_DIR}"
CMAKE_DEFS=(
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
  -DHalide_DIR="${HALIDE_INSTALL_PATH}/lib/cmake/Halide"
  -DHalideHelpers_DIR="${HALIDE_INSTALL_PATH}/lib/cmake/HalideHelpers"
  -DHalide_TARGET="${HL_TARGET}"
)

# Ask the provided Halide install for the host target
HL_HOST_TARGET="host"
get_host_target="${HALIDE_INSTALL_PATH}/bin/get_host_target"
[ -x "${get_host_target}" ] && HL_HOST_TARGET="$("${get_host_target}" | cut -d- -f1-3)"

# Cross compile when HL_TARGET does not match the host target.
if [[ ! "${HL_TARGET}" =~ ^host*|${HL_HOST_TARGET}* ]]; then
  SOURCE_DIR="${HANNK_DIR}/cmake/superbuild"
  CMAKE_DEFS=(
    "${CMAKE_DEFS[@]}"
    -DHANNK_HOST_Halide_DIR="${HALIDE_INSTALL_PATH}/lib/cmake/Halide"
    -DHANNK_HOST_HalideHelpers_DIR="${HALIDE_INSTALL_PATH}/lib/cmake/HalideHelpers"
  )

  # Special settings for cross-compiling targets with known quirks
  if [[ "${HL_TARGET}" =~ ^arm-64-android.* ]]; then
    : "${ANDROID_PLATFORM:=21}"
    [ -d "${ANDROID_NDK_ROOT}" ] || die "Must set ANDROID_NDK_ROOT"

    CMAKE_DEFS=(
      "${CMAKE_DEFS[@]}"
      -DHANNK_CROSS_CMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake"
      -DANDROID_ABI=arm64-v8a
      -DANDROID_PLATFORM="${ANDROID_PLATFORM}"
      # Required because TFLite's internal Eigen tries to compile an unnecessary BLAS with the system Fortran compiler.
      -DCMAKE_Fortran_COMPILER=NO
    )
  elif [[ "${HL_TARGET}" =~ ^wasm-32-wasmrt.* ]]; then
    [ -d "${EMSDK}" ] || die "Must set EMSDK"
    [ -x "${NODE_JS_EXECUTABLE}" ] || die "Must set NODE_JS_EXECUTABLE (version 16.13+ required)"

    CMAKE_DEFS=(
      "${CMAKE_DEFS[@]}"
      -DHANNK_CROSS_CMAKE_TOOLCHAIN_FILE="${EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
      -DNODE_JS_EXECUTABLE="${NODE_JS_EXECUTABLE}"
    )
  fi
fi

cmake \
  -G "${CMAKE_GENERATOR}" \
  -S "${SOURCE_DIR}" \
  -B "${BUILD_DIR}" \
  "${CMAKE_DEFS[@]}" \
  "$@"
