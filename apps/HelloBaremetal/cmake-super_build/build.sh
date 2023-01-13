#!/bin/bash
set -eo pipefail

# Prerequisite :
#     Halide is installed system-wide in your host machine
#      or environmental variable `HALIDE_ROOT` is set to point your own Halide installation

cd "$(dirname ${BASH_SOURCE[0]})"
readonly TOOLCHAIN_FILE="../cmake/toolchain.noos-arm32-sample.cmake"

rm -rf build

cmake -DCMAKE_PREFIX_PATH=${HALIDE_ROOT} \
    -DAPP_TOOLCHAIN_FILE=${TOOLCHAIN_FILE} \
    -DBAREMETAL=ON \
    -B build -S .

cmake --build build/
