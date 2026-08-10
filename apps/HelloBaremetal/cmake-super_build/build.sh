#!/bin/bash
set -eo pipefail

# Prerequisite :
#     Halide is installed system-wide in your host machine or discoverable via CMAKE_PREFIX_PATH

cd "$(dirname "${BASH_SOURCE[0]}")"
readonly TOOLCHAIN_FILE="${PWD}/../cmake/toolchain.noos-arm32-sample.cmake"

cmake -S . -B build -DAPP_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" --fresh
cmake --build build
