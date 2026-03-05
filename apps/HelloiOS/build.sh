#!/usr/bin/env bash

set -euo pipefail

die() {
    echo "$@" >&2
    exit 1
}

if [[ $# -ne 1 ]]; then
    die "Usage: $0 <Halide_TARGET>"
fi

HALIDE_TARGET="$1"
if [[ "${HALIDE_TARGET}" == "host" ]]; then
    die "Halide_TARGET=host is not valid for the iOS app build."
fi

if [[ "${HALIDE_TARGET}" == *simulator* ]]; then
    CMAKE_OSX_SYSROOT=iphonesimulator
else
    CMAKE_OSX_SYSROOT=iphoneos
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" > /dev/null 2>&1 && pwd)"

: "${CMAKE_BUILD_TYPE:=Release}"
: "${CMAKE_OSX_DEPLOYMENT_TARGET:=15.0}"
: "${HELLOIOS_DEVELOPMENT_TEAM:=}"

: "${HOST_BUILD_DIR:=${SCRIPT_DIR}/build/host}"
: "${IOS_BUILD_DIR:=${SCRIPT_DIR}/build/${HALIDE_TARGET}}"

[[ -n "${Halide_ROOT:-}" ]] || die "Set Halide_ROOT in the environment to the Halide install prefix."
export HalideHelpers_ROOT="${Halide_ROOT}"

## Phase 1: Host build of the Halide generators.

cmake -S "${SCRIPT_DIR}" -B "${HOST_BUILD_DIR}" -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
cmake --build "${HOST_BUILD_DIR}"

## Phase 2: Target build of the iOS app, using the host-built Halide generators.

cmake -G Xcode -S "${SCRIPT_DIR}" -B "${IOS_BUILD_DIR}" \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${CMAKE_OSX_DEPLOYMENT_TARGET}" \
    -DCMAKE_OSX_SYSROOT="${CMAKE_OSX_SYSROOT}" \
    -DHelloiOS-halide_generators_ROOT="${HOST_BUILD_DIR}" \
    -DHalide_TARGET="${HALIDE_TARGET}" \
    -DHELLOIOS_DEVELOPMENT_TEAM="${HELLOIOS_DEVELOPMENT_TEAM}"

cmake --build "${IOS_BUILD_DIR}" --config "${CMAKE_BUILD_TYPE}"

echo "Built HelloiOS.app under ${IOS_BUILD_DIR}/${CMAKE_BUILD_TYPE}-${CMAKE_OSX_SYSROOT}/"
