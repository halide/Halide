#!/bin/bash

if [[ $# -ne 2 && $# -ne 3 ]]; then
    echo "Usage: $0 max_iterations resume app"
    exit
fi

trap ctrl_c INT

source $(dirname $0)/../scripts/utils.sh

BEST_SCHEDULES_DIR=$(dirname $0)/best

find_halide HALIDE_ROOT

build_autoscheduler_tools ${HALIDE_ROOT}

MAX_ITERATIONS=${1}
RESUME=${2}
APP=${3}

export CXX="ccache c++"

export HL_MACHINE_PARAMS=80,24000000,160

export HL_PERMIT_FAILED_UNROLL=1
export HL_TARGET=host-cuda

if [ -z ${SAMPLES_DIR} ]; then
    SAMPLES_DIR_NAME=autotuned_samples
else
    SAMPLES_DIR_NAME=${SAMPLES_DIR}
fi

CURRENT_DATE_TIME="`date +%Y-%m-%d-%H-%M-%S`";

if [ -z $APP ]; then
    APPS="resnet_50_blockwise bgu bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate_generator conv_layer cuda_mat_mul iir_blur_generator"
else
    APPS=$APP
fi

NUM_APPS=0
for app in $APPS; do
    NUM_APPS=$((NUM_APPS + 1))
done
echo "Autotuning on $APPS for $MAX_ITERATIONS iteration(s)"

for app in $APPS; do
    APP_DIR="${HALIDE_ROOT}/apps/${app}"

    if [ ${RESUME} -eq 1 ]; then
        SAMPLES_DIR=$(ls -ld $APP_DIR/${SAMPLES_DIR_NAME}* | tail -n 1 | rev | cut -d" " -f 1 | rev)
        echo "Resuming from existing run: ${SAMPLES_DIR}"
    else
        SAMPLES_DIR_NAME=${SAMPLES_DIR_NAME}-${CURRENT_DATE_TIME}
        SAMPLES_DIR="${APP_DIR}/${SAMPLES_DIR_NAME}"
        echo "Starting new run in: ${SAMPLES_DIR}"
    fi

    OUTPUT_FILE="${SAMPLES_DIR}/autotune_out.txt"
    PREDICTIONS_FILE="${SAMPLES_DIR}/predictions"
    BEST_TIMES_FILE="${SAMPLES_DIR}/best_times"
    WEIGHTS_FILE="${SAMPLES_DIR}/updated.weights"

    mkdir -p ${SAMPLES_DIR}
    touch ${OUTPUT_FILE}

    ITERATION=1

    while [[ DONE -ne 1 ]]; do
        SAMPLES_DIR=${SAMPLES_DIR} make -C ${APP_DIR} autotune | tee -a ${OUTPUT_FILE}

        if [[ $ITERATION -ge $MAX_ITERATIONS ]]; then
            break
        fi

        ITERATION=$((ITERATION + 1))
    done

    predict_all ${HALIDE_ROOT} ${SAMPLES_DIR} ${WEIGHTS_FILE} ${PREDICTIONS_FILE}
    extract_best_times ${HALIDE_ROOT} ${SAMPLES_DIR} ${BEST_TIMES_FILE}
    bash $(dirname $0)/../scripts/average_times.sh ${SAMPLES_DIR} >> ${OUTPUT_FILE}

    save_best_schedule_result ${BEST_SCHEDULES_DIR} ${SAMPLES_DIR}
done

print_best_schedule_times $(dirname $0)/best

function ctrl_c() {
    echo "Trap: CTRL+C received, exiting"
    pkill -P $$

    for app in $APPS; do
        ps aux | grep ${app}.generator | awk '{print $2}' | xargs kill

        if [ ${RESUME} -eq 1 ]; then
            SAMPLES_DIR=$(ls -ld $APP_DIR/${SAMPLES_DIR_NAME}* | tail -n 1 | rev | cut -d" " -f 1 | rev)
        else
            SAMPLES_DIR_NAME=${SAMPLES_DIR_NAME}-${CURRENT_DATE_TIME}
            SAMPLES_DIR="${APP_DIR}/${SAMPLES_DIR_NAME}"
        fi
        save_best_schedule_result ${BEST_SCHEDULES_DIR} ${SAMPLES_DIR}
    done

    print_best_schedule_times $(dirname $0)/best
    exit
}
