#!/bin/bash
set -eo pipefail

# Prerequisite :
#     Halide is installed system-wide in your host machine
#      or environmental variable `HALIDE_ROOT` is set to point your own Halide installation

cd "$(dirname ${BASH_SOURCE[0]})"
readonly TOOLCHAIN_FILE="../cmake/toolchain.noos-arm32-sample.cmake"
readonly GEN_PACKAGE="HelloBaremetal-add_generator"

rm -rf build-host
rm -rf build-target

# Step 1
# Build generator executable with host compiler
# and export as a package for CMake find_package()
cmake -DCMAKE_PREFIX_PATH=${HALIDE_ROOT} \
    -DGEN_PACKAGE=${GEN_PACKAGE} \
    -B build-host -S .

cmake --build build-host/ \
    --target ${GEN_PACKAGE}

# Step 2
# Build application with cross compiler,
# where the generator executable built in Step 1 is just imported and called
cmake -DCMAKE_PREFIX_PATH=${HALIDE_ROOT} \
    -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE} \
    -DGEN_PACKAGE=${GEN_PACKAGE} \
    -D${GEN_PACKAGE}_ROOT:FILEPATH=build-host \
    -DBAREMETAL=ON \
    -B build-target -S .

cmake --build build-target/
