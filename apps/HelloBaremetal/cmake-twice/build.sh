#!/bin/bash
set -eo pipefail

# Prerequisite :
#     Halide is installed system-wide in your host machine or discoverable via CMAKE_PREFIX_PATH

cd "$(dirname ${BASH_SOURCE[0]})"
readonly TOOLCHAIN_FILE="${PWD}/../cmake/toolchain.noos-arm32-sample.cmake"
readonly GEN_PACKAGE="HelloBaremetal-add_generator"

rm -rf build-host
rm -rf build-target

# Step 1
# Build generator executable with host compiler
# and export as a package for CMake find_package()
cmake -S . -B build-host -DGEN_PACKAGE="${GEN_PACKAGE}"

cmake --build build-host/ --target "${GEN_PACKAGE}"

# Step 2
# Build application with cross compiler,
# where the generator executable built in Step 1 is just imported and called
cmake -S . -B build-target \
	    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
	    -DGEN_PACKAGE="${GEN_PACKAGE}" \
	    -D${GEN_PACKAGE}_ROOT:FILEPATH=build-host

cmake --build build-target/
