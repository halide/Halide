#!/bin/bash

if [[ $# -ne 0 && $# -ne 1 ]]; then
    echo "Usage: $0 app"
    exit
fi

APP=$1

source $(dirname $0)/../scripts/utils.sh

BEST_SCHEDULES_DIR=$(dirname $0)/best

find_halide HALIDE_ROOT

export HL_MACHINE_PARAMS=80,24000000,160
export HL_PERMIT_FAILED_UNROLL=1

if [ -z ${HL_TARGET} ]; then
    HL_TARGET=host-cuda
fi

export HL_TARGET=${HL_TARGET}

function ctrl_c() {
    echo "Trap: CTRL+C received, exiting"
    pkill -P $$
    exit
}

trap ctrl_c INT

if [ -z $APP ]; then
    APPS="bgu bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate conv_layer cuda_mat_mul iir_blur depthwise_separable_conv"
else
    APPS=$APP
fi

if [ $(uname -s) = "Darwin" ]; then
    LOCAL_CORES=`sysctl -n hw.ncpu`
else
    LOCAL_CORES=`nproc`
fi

echo Local number of cores detected as ${LOCAL_CORES}

for app in $APPS; do
    echo "Benchmarking $app"
    APP_DIR="${HALIDE_ROOT}/apps/${app}"

    make -C ${APP_DIR} clean
    IMAGES="${HALIDE_ROOT}/apps/images" HL_TARGET=host-cuda OPTIMIZE=-O3 NO_AUTO_SCHEDULE=1 NO_GRADIENT_AUTO_SCHEDULE=1 make -C ${APP_DIR} test -j${LOCAL_CORES} | grep "Manual time"
done

