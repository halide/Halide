#!/bin/sh -x
mkdir -p ~/halide_build
cd ~/halide_build
ln -s -f /Halide/Makefile .
make -j 3
make -k test_opengl
