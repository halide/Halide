#!/bin/bash -x
HALIDE_TOP=`pwd`/../..
rm -rf gradient.s Hexagon_vsplat_vaddw
g++ Hexagon_vsplat_vaddw.cpp -g -I ${HALIDE_TOP}/include -L ${HALIDE_TOP}/bin -lHalide -lpthread -ldl -o Hexagon_vsplat_vaddw -O0 -std=c++11

if [ $? == 0 ]
then
    export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:$LD_LIBRARY_PATH
#   export HL_DEBUG_CODEGEN=4
#/pkg/qct/software/gnu64/gdb/7.2/bin/gdb  \
    ./Hexagon_vsplat_vaddw $*

    if  grep -q vadd gradient.s; then
        if grep -q "vsplat(" gradient.s; then
            echo "vsplat and vadd test PASSED"
        fi
    fi
fi
