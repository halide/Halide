#!/bin/bash

HALIDE_TOP=`pwd`/../..
rm -rf *.s *.bc *.out
export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:/pkg/qct/software/gnu64/mpc/0.9/lib:/pkg/qct/software/gnu64/gcc/4.8.2/lib64:${HEX_TOOLS}/lib
myname=`basename $0`
echo "$myname: Compiling all .cpp files first."
for test in `cat testlist`
do
    echo "$myname: Compiling $test.cpp."
    g++ ${test}.cpp -g -I ${HALIDE_TOP}/include -L ${HALIDE_TOP}/bin -lHalide -lpthread -ldl -o ${test}.out -O0
    echo "$myname: Creating test - ${test}.out"
done

LIT=`${LLVM_CONFIG} --src-root`/utils/lit/lit.py

if [ ! -f ${LIT} ]; then
    echo "lit.py not found"
    exit 1
fi

FILECHECK_PATH=`$LLVM_CONFIG --bindir`
FILECHECK=${FILECHECK_PATH}/FileCheck
echo "$myname: Over to lit.py now"
${LIT} `pwd` --path=${FILECHECK_PATH} -v