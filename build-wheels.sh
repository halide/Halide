#!/bin/bash

export CIBW_PLATFORM=linux

export CIBW_ARCHS="x86_64 i686 aarch64"
export CIBW_BUILD="cp38-manylinux* cp39-manylinux* cp310-manylinux*"

export CIBW_MANYLINUX_X86_64_IMAGE=ghcr.io/halide/manylinux2014_x86_64-llvm:15.0.1
export CIBW_MANYLINUX_I686_IMAGE=ghcr.io/halide/manylinux2014_i686-llvm:15.0.1
export CIBW_MANYLINUX_AARCH64_IMAGE=ghcr.io/halide/manylinux2014_aarch64-llvm:15.0.1

export CIBW_BEFORE_ALL="\
  cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release -DWITH_DOCS=NO -DWITH_PYTHON_BINDINGS=NO -DWITH_TESTS=NO \
    -DWITH_TUTORIALS=NO -DWITH_UTILS=NO && \
  cmake --build build --target install"

cibuildwheel --print-build-identifiers
