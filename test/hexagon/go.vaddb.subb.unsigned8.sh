#!/bin/bash


HALIDE_TOP=`pwd`/../..
rm -rf vsubb.s vaddb.s vsubb.bc vadd.bc
export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:/pkg/qct/software/gnu64/mpc/0.9/lib:/pkg/qct/software/gnu64/gcc/4.8.2/lib64:${HEX_TOOLS}/lib
g++  vaddb_subb_unsigned.cpp -g -I ${HALIDE_TOP}/include -L ${HALIDE_TOP}/bin -lHalide -lpthread -ldl -o Hexagon_vaddb_vsubb_unsigned -O0

if [ $? == 0 ]
then
    export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:$LD_LIBRARY_PATH
    export HL_DEBUG_CODEGEN=4
    ./Hexagon_vaddb_vsubb_unsigned
    if  grep -q vsub vsubb_unsigned.s; then
        echo "vsubb (u8*64) test PASSED"
    else
        echo "vsubb (u8*64) test FAILED"
    fi
    if  grep -q vadd vaddb_unsigned.s; then
        echo "vaddb (u8*64) test PASSED"
    else
        echo "vaddb (u8*64) test FAILED"
    fi
fi
