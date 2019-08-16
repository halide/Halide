#!/bin/bash

if [[ $# -ne 2 && $# -ne 3 ]]; then
    echo "Usage: $0 mode [greedy|beam_search] max_iterations app"
    exit
fi

HALIDE=$(cd $(dirname $0)/../../..; pwd)

MODE=${1}
MAX_ITERATIONS=${2}
APP=${3}

if [ $MODE == "greedy" ]; then
    BEAM_SIZE=1
    NUM_PASSES=1
elif [ $MODE == "beam_search" ]; then
    BEAM_SIZE=32
    NUM_PASSES=5
else
    echo "Unknown mode: ${MODE}"
    exit
fi

echo "Using ${MODE} mode with beam_size=${BEAM_SIZE} and num_passes=${NUM_PASSES}"

export HL_BEAM_SIZE=${BEAM_SIZE}
export HL_NUM_PASSES=${NUM_PASSES}

export CXX="ccache c++"

export HL_MACHINE_PARAMS=80,24000000,160

export HL_PERMIT_FAILED_UNROLL=1
export HL_WEIGHTS_DIR=${HALIDE}/apps/autoscheduler/gpu_weights
export HL_TARGET=host-cuda

# no random dropout
export HL_RANDOM_DROPOUT=100

if [ -z $APP ]; then
    APPS="resnet_50_blockwise bgu bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate_generator conv_layer mat_mul_generator iir_blur_generator"
else
    APPS=$APP
fi

NUM_APPS=0
for app in $APPS; do
    NUM_APPS=$((NUM_APPS + 1))
done
echo "Autotuning on $APPS for $MAX_ITERATIONS iteration(s)"

MAX_ITERATIONS=$((MAX_ITERATIONS * NUM_APPS))

ITERATION=1

SAMPLES_DIR="${HALIDE}/apps/${app}/autotuned_samples_${MODE}"
OUTPUT_FILE="${SAMPLES_DIR}/autotune_out.txt"

for app in $APPS; do
    mkdir -p ${SAMPLES_DIR}
    touch ${OUTPUT_FILE}
done

while [ 1 ]; do
    for app in $APPS; do
        SECONDS=0
        # 15 mins of autotuning per app, round robin
        while [[ SECONDS -lt 900 ]]; do
            echo ${SAMPLES_DIR}
            SAMPLES_DIR=${SAMPLES_DIR} make -C ${HALIDE}/apps/${app} autotune | tee -a ${OUTPUT_FILE}

            if [[ $MAX_ITERATIONS -ne 0 && $ITERATION -ge $MAX_ITERATIONS ]]; then
                exit
            fi

            ITERATION=$((ITERATION + 1))
        done
    done
done
