#!/bin/bash

# This script will generate a batch of data using the autoscheduler, retraining
# the cost model after each batch. It can be used for generating training data or
# for autotuning on an individual app.
#
# It is a wrapper around anderson2021_autotune_loop.sh, which handles compiling, benchmarking,
# and retraining the cost model. This file makes the process more user friendly
# by providing statistics, support for resuming previous batches, autotuning
# across multiple apps, etc.
#
# It assumes that the autoscheduler itself and any apps to be autoscheduled have
# already been built and the resulting files are stored in halide_build_dir.
# Using CMake is recommended because this script assumes that the given
# halide_build_dir has the same structure that the CMake build will produce
#
# Arguments:
# halide_build_dir [path] - path where Halide is built
# max_iterations [int] - the number of batches to generate. The cost model is
# retrained after each
# resume [0|1] - resume using the previously generated samples or start a new run?
# train_only [0|1] - don't generate new data, just retrain the cost model with
# existing samples
# predict_only [0|1] - don't generate new data, just predict the costs of the existing
# samples
# parallelism [int] - the number of streaming multiprocessors in the target GPU
# app [string; optional] - the individual application (in Halide/apps/) to generate data for. If
# not provided, it will generate a data for all the apps in the list below

if [[ $# -ne 6 && $# -ne 7 ]]; then
    echo "Usage: $0 halide_build_dir max_iterations resume train_only predict_only parallelism app"
    exit
fi

set -eu

if [ -z ${BASH_VERSION+x} ]; then
    echo "${0} should be run as a bash script"
    exit
fi

HALIDE_BUILD_DIR=${1}
MAX_ITERATIONS=${2}
RESUME=${3}
TRAIN_ONLY=${4}
PREDICT_ONLY=${5}
PARALLELISM=${6}

if [ -z ${7+x} ]; then
    APPS="bgu bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate conv_layer cuda_mat_mul iir_blur depthwise_separable_conv"
else
    APPS=${7}
fi

if [ -z ${CXX+x} ]; then
    echo The CXX environment variable must be set. Exiting...
    exit
fi

if command -v ccache > /dev/null; then
    echo "ccache detected and will be used"
    export CXX="ccache ${CXX}"
fi

AUTOSCHEDULER_SRC_DIR=$(dirname $0)
SCRIPTS_DIR="${AUTOSCHEDULER_SRC_DIR}/scripts"
source ${SCRIPTS_DIR}/utils.sh
make_dir_path_absolute $(dirname $0) AUTOSCHEDULER_SRC_DIR

get_halide_src_dir ${AUTOSCHEDULER_SRC_DIR} HALIDE_SRC_DIR
get_autoscheduler_build_dir ${HALIDE_BUILD_DIR} AUTOSCHEDULER_BUILD_DIR

echo "HALIDE_SRC_DIR = ${HALIDE_SRC_DIR}"
echo "HALIDE_BUILD_DIR = ${HALIDE_BUILD_DIR}"
echo "AUTOSCHEDULER_SRC_DIR = ${AUTOSCHEDULER_SRC_DIR}"
echo "AUTOSCHEDULER_BUILD_DIR = ${AUTOSCHEDULER_BUILD_DIR}"
echo

BEST_SCHEDULES_DIR=${AUTOSCHEDULER_SRC_DIR}/best
export HL_PERMIT_FAILED_UNROLL=1

if [ -z ${HL_TARGET+x} ]; then
    get_host_target ${AUTOSCHEDULER_BUILD_DIR} HL_TARGET
    HL_TARGET=${HL_TARGET}-cuda-cuda_capability_70
fi

echo "HL_TARGET set to ${HL_TARGET}"
echo

if [[ $PREDICT_ONLY == 1 && $TRAIN_ONLY == 1 ]]; then
    echo "At most one of train_only and predict_only can be set to 1."
    exit
fi

if [[ $PREDICT_ONLY == 1 ]]; then
    echo "Predict only mode: ON"
fi

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

NUM_APPS=0
for app in $APPS; do
    NUM_APPS=$((NUM_APPS + 1))
done

echo "Autotuning on $APPS for $MAX_ITERATIONS iteration(s)"

for app in $APPS; do
    SECONDS=0
    APP_DIR="${HALIDE_SRC_DIR}/apps/${app}"
    if [ ! -d $APP_DIR ]; then
        echo "App ${APP_DIR} not found. Skipping..."
        continue
    fi

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

    if [[ $TRAIN_ONLY == 1 ]]; then
        if [[ ! -d ${SAMPLES_DIR} || -z "$(ls -A ${SAMPLES_DIR})" ]]; then
            echo "No samples found in ${SAMPLES_DIR}. Skipping..."
            continue
        fi
    fi

    mkdir -p ${SAMPLES_DIR}
    touch ${OUTPUT_FILE}

    GENERATOR_BUILD_DIR=${HALIDE_BUILD_DIR}/apps/${app}

    if [[ ${app} = "cuda_mat_mul" ]]; then
        app="mat_mul"
    fi

    GENERATOR=${GENERATOR_BUILD_DIR}/${app}.generator
    if [ ! -f $GENERATOR ]; then
        echo "Generator ${GENERATOR} not found. Skipping..."
        continue
    fi
    echo

    if [[ $PREDICT_ONLY != 1 ]]; then
        NUM_BATCHES=${MAX_ITERATIONS} \
        TRAIN_ONLY=${TRAIN_ONLY} \
        SAMPLES_DIR=${SAMPLES_DIR} \
        HL_DEBUG_CODEGEN=0 \
        bash ${AUTOSCHEDULER_SRC_DIR}/anderson2021_autotune_loop.sh \
            ${GENERATOR} \
            ${app} \
            ${HL_TARGET} \
            ${AUTOSCHEDULER_SRC_DIR}/baseline.weights \
            ${HALIDE_BUILD_DIR} \
            ${PARALLELISM} \
            ${TRAIN_ONLY} | tee -a ${OUTPUT_FILE}
    fi

    WEIGHTS_FILE="${SAMPLES_DIR}/updated.weights"
    predict_all ${HALIDE_SRC_DIR} ${HALIDE_BUILD_DIR} ${SAMPLES_DIR} ${WEIGHTS_FILE} ${PREDICTIONS_WITH_FILENAMES_FILE} 1 ${LIMIT:-0} ${PARALLELISM}
    awk -F", " '{printf("%f, %f\n", $2, $3);}' ${PREDICTIONS_WITH_FILENAMES_FILE} > ${PREDICTIONS_FILE}

    echo "Computing average statistics..."
    bash ${SCRIPTS_DIR}/average_times.sh ${SAMPLES_DIR} >> ${OUTPUT_FILE}

    echo "Total autotune time (s): ${SECONDS}" >> ${OUTPUT_FILE}

    save_best_schedule_result ${BEST_SCHEDULES_DIR} ${SAMPLES_DIR}
done

echo
print_best_schedule_times $(dirname $0)/best
