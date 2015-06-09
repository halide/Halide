#!/bin/bash -x
HALIDE_TOP=`pwd`/../..
echo $HALIDE_TOP
cd ./apps/camera_pipe
pwd

export LD_LIBRARY_PATH=$TOP/llvm/Tools/lib:/pkg/qct/software/gnu64/gcc/4.8.2/lib64:/pkg/qct/software/gnu64/mpc/0.9/lib:/pkg/qct/software/gnu64/mpfr/2.4.2/lib:/pkg/qct/software/gnu64/gmp/4.3.2/lib

/pkg/qct/software/gnu64/gcc/4.8.2/bin/g++ camera_pipe.cpp -g -I${HALIDE_TOP}/test/hexagon -I ${HALIDE_TOP}/include -L ${HALIDE_TOP}/bin -lHalide -lpthread -ldl -o Hexagon_camera_pipe -O0 -DASSEMBLY -std=c++11

export LD_LIBRARY_PATH=${HALIDE_TOP}/bin:$LD_LIBRARY_PATH

if [ $# -eq 0 ]; then
  export SCHED=3
else
  export SCHED=$1
fi

#   /pkg/qct/software/gnu64/gdb/7.2/bin/gdb \
   ./Hexagon_camera_pipe 8 $SCHED

