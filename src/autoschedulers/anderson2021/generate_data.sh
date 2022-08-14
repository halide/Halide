#!/bin/bash

# This script will generate a batch of data using the autoscheduler, retraining
# the cost model after each batch. It can be used for generating training data or
# for autotuning on an individual app.
# It is a wrapper around autotune_loop.sh, which handles compiling, benchmarking,
# and retraining the cost model. This file makes the process more user friendly
# by providing statistics, support for resuming previous batches, autotuning
# across multiple apps, etc.
#
# Arguments:
# max_iterations - the number of batches to generate. The cost model is
# retrained after each
# resume - resume using the previously generated samples or start a new run?
# train_only - don't generate new data, just retrain the cost model with
# existing samples
# predict_only - don't generate new data, just predict the costs of the existing
# samples
# app - the individual application (in Halide/apps/) to generate data for. If
# not provided, it will generate a data for all the apps in the list below

if [[ $# -ne 4 && $# -ne 5 ]]; then
    echo "Usage: $0 max_iterations resume train_only predict_only app"
    exit
fi

set -e

MAX_ITERATIONS=${1}
RESUME=${2}
TRAIN_ONLY=${3}
PREDICT_ONLY=${4}
APP=${5}

if [[ $PREDICT_ONLY == 1 && $TRAIN_ONLY == 1 ]]; then
    echo "At most one of train_only and predict_only can be set to 1."
    exit
fi

if [[ $PREDICT_ONLY == 1 ]]; then
    echo "Predict only mode: ON"
fi

SCRIPTS_DIR="$(dirname $0)/scripts"
source ${SCRIPTS_DIR}/utils.sh

BEST_SCHEDULES_DIR=$(dirname $0)/best

find_halide HALIDE_ROOT

build_autoscheduler_tools ${HALIDE_ROOT}
get_absolute_autoscheduler_bin_dir ${HALIDE_ROOT} AUTOSCHED_BIN
get_autoscheduler_dir ${HALIDE_ROOT} AUTOSCHED_SRC

export CXX="ccache ${CXX}"

export HL_MACHINE_PARAMS=80,24000000,160

export HL_PERMIT_FAILED_UNROLL=1

export AUTOSCHED_BIN=${AUTOSCHED_BIN}
echo "AUTOSCHED_BIN set to ${AUTOSCHED_BIN}"
echo

if [ ! -v HL_TARGET ]; then
    get_host_target ${HALIDE_ROOT} HL_TARGET
    HL_TARGET=${HL_TARGET}-cuda-cuda_capability_70
fi

export HL_TARGET=${HL_TARGET}

echo "HL_TARGET set to ${HL_TARGET}"

DEFAULT_SAMPLES_DIR_NAME="${SAMPLES_DIR:-autotuned_samples}"

CURRENT_DATE_TIME="`date +%Y-%m-%d-%H-%M-%S`";

function ctrl_c() {
    echo "Trap: CTRL+C received, exiting"
    pkill -P $$

    for app in $APPS; do
        ps aux | grep ${app}.generator | awk '{print $2}' | xargs kill

        unset -v LATEST_SAMPLES_DIR
        for f in "$APP_DIR/${DEFAULT_SAMPLES_DIR_NAME}"*; do
            if [[ ! -d $f ]]; then
               continue
           fi

            if [[ -z ${LATEST_SAMPLES_DIR+x} || $f -nt $LATEST_SAMPLES_DIR ]]; then
                LATEST_SAMPLES_DIR=$f
            fi
        done

        if [[ ${RESUME} -eq 1 && -z ${LATEST_SAMPLES_DIR+x} ]]; then
            SAMPLES_DIR=${LATEST_SAMPLES_DIR}
        else
            while [[ 1 ]]; do
                SAMPLES_DIR_NAME=${DEFAULT_SAMPLES_DIR_NAME}-${CURRENT_DATE_TIME}
                SAMPLES_DIR="${APP_DIR}/${SAMPLES_DIR_NAME}"

                if [[ ! -d ${SAMPLES_DIR} ]]; then
                    break
                fi

                sleep 1
                CURRENT_DATE_TIME="`date +%Y-%m-%d-%H-%M-%S`";
            done
        fi
        save_best_schedule_result ${BEST_SCHEDULES_DIR} ${SAMPLES_DIR}
    done

    print_best_schedule_times $(dirname $0)/best
    exit
}

trap ctrl_c INT

if [ -z $APP ]; then
    APPS="bgu bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate conv_layer cuda_mat_mul iir_blur depthwise_separable_conv"
else
    APPS=${APP}
fi

NUM_APPS=0
for app in $APPS; do
    NUM_APPS=$((NUM_APPS + 1))
done

echo "Autotuning on $APPS for $MAX_ITERATIONS iteration(s)"

for app in $APPS; do
    SECONDS=0
    APP_DIR="${HALIDE_ROOT}/apps/${app}"

    unset -v LATEST_SAMPLES_DIR
    for f in "$APP_DIR/${DEFAULT_SAMPLES_DIR_NAME}"*; do
        if [[ ! -d $f ]]; then
           continue
       fi

        if [[ -z ${LATEST_SAMPLES_DIR+x} || $f -nt $LATEST_SAMPLES_DIR ]]; then
            LATEST_SAMPLES_DIR=$f
        fi
    done

    if [[ ${RESUME} -eq 1 && -z ${LATEST_SAMPLES_DIR+x} ]]; then
        SAMPLES_DIR=${LATEST_SAMPLES_DIR}
        echo "Resuming from existing run: ${SAMPLES_DIR}"
    else
        while [[ 1 ]]; do
            SAMPLES_DIR_NAME=${DEFAULT_SAMPLES_DIR_NAME}-${CURRENT_DATE_TIME}
            SAMPLES_DIR="${APP_DIR}/${SAMPLES_DIR_NAME}"

            if [[ ! -d ${SAMPLES_DIR} ]]; then
                break
            fi

            sleep 1
            CURRENT_DATE_TIME="`date +%Y-%m-%d-%H-%M-%S`";
        done
        SAMPLES_DIR="${APP_DIR}/${SAMPLES_DIR_NAME}"
        echo "Starting new run in: ${SAMPLES_DIR}"
    fi

    OUTPUT_FILE="${SAMPLES_DIR}/autotune_out.txt"
    PREDICTIONS_FILE="${SAMPLES_DIR}/predictions"
    PREDICTIONS_WITH_FILENAMES_FILE="${SAMPLES_DIR}/predictions_with_filenames"
    BEST_TIMES_FILE="${SAMPLES_DIR}/best_times"

    mkdir -p ${SAMPLES_DIR}
    touch ${OUTPUT_FILE}

    if [[ ${app} = "cuda_mat_mul" ]]; then
        app="mat_mul"
    fi

    GENERATOR=bin/host/${app}.generator
    make -C ${APP_DIR} ${GENERATOR}

    if [[ $PREDICT_ONLY != 1 ]]; then
        NUM_BATCHES=${MAX_ITERATIONS} \
        TRAIN_ONLY=${TRAIN_ONLY} \
        SAMPLES_DIR=${SAMPLES_DIR} \
        HARDWARE_PARALLELISM=80 \
        SAMPLES_DIR=${SAMPLES_DIR} \
        HL_DEBUG_CODEGEN=0 \
        HL_SHARED_MEMORY_LIMIT=48 \
        bash ${AUTOSCHED_SRC}/autotune_loop.sh \
            ${APP_DIR}/${GENERATOR} \
            ${app} \
            ${HL_TARGET} \
            ${AUTOSCHED_SRC}/baseline.weights \
            ${AUTOSCHED_BIN} \
            ${TRAIN_ONLY} | tee -a ${OUTPUT_FILE}
    fi

    WEIGHTS_FILE="${SAMPLES_DIR}/updated.weights"
    predict_all ${HALIDE_ROOT} ${SAMPLES_DIR} ${WEIGHTS_FILE} ${PREDICTIONS_WITH_FILENAMES_FILE} 1 ${LIMIT:-0}
    awk -F", " '{printf("%f, %f\n", $2, $3);}' ${PREDICTIONS_WITH_FILENAMES_FILE} > ${PREDICTIONS_FILE}

    echo "Computing average statistics..."
    bash ${SCRIPTS_DIR}/average_times.sh ${SAMPLES_DIR} >> ${OUTPUT_FILE}

    echo "Total autotune time (s): ${SECONDS}" >> ${OUTPUT_FILE}

    save_best_schedule_result ${BEST_SCHEDULES_DIR} ${SAMPLES_DIR}
done

print_best_schedule_times $(dirname $0)/best
