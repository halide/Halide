#!/bin/bash

if [[ $# -ne 5 && $# -ne 6 ]]; then
    echo "Usage: $0 max_iterations resume train_only predict_only compare_with_metrics app"
    exit
fi

MAX_ITERATIONS=${1}
RESUME=${2}
TRAIN_ONLY=${3}
PREDICT_ONLY=${4}
COMPARE_WITH_METRICS=${5}
APP=${6}

if [[ $PREDICT_ONLY == 1 && $TRAIN_ONLY == 1 ]]; then
    echo "At most one of train_only and predict_only can be set to 1."
    exit
fi

if [[ $PREDICT_ONLY == 1 ]]; then
    echo "Predict only mode: ON"
fi

source $(dirname $0)/../scripts/utils.sh

BEST_SCHEDULES_DIR=$(dirname $0)/best

find_halide HALIDE_ROOT

build_autoscheduler_tools ${HALIDE_ROOT}

export CXX="ccache c++"

export HL_MACHINE_PARAMS=80,24000000,160

export HL_PERMIT_FAILED_UNROLL=1

if [ -z ${HL_TARGET} ]; then
    get_host_target ${HALIDE_ROOT} HL_TARGET
    HL_TARGET=${HL_TARGET}-cuda
fi

export HL_TARGET=${HL_TARGET}

echo "HL_TARGET set to ${HL_TARGET}"

if [ -z ${SAMPLES_DIR} ]; then
    DEFAULT_SAMPLES_DIR_NAME=autotuned_samples
else
    DEFAULT_SAMPLES_DIR_NAME=${SAMPLES_DIR}
fi

CURRENT_DATE_TIME="`date +%Y-%m-%d-%H-%M-%S`";

function ctrl_c() {
    echo "Trap: CTRL+C received, exiting"
    pkill -P $$

    for app in $APPS; do
        ps aux | grep ${app}.generator | awk '{print $2}' | xargs kill

        LATEST_SAMPLES_DIR=$(ls -ld $APP_DIR/${DEFAULT_SAMPLES_DIR_NAME}* | tail -n 1 | rev | cut -d" " -f 1 | rev)
        if [[ ${RESUME} -eq 1 && -d ${LATEST_SAMPLES_DIR} ]]; then
            SAMPLES_DIR=${LATEST_SAMPLES_DIR}
        else
            SAMPLES_DIR_NAME=${DEFAULT_SAMPLES_DIR_NAME}-${CURRENT_DATE_TIME}
            SAMPLES_DIR="${APP_DIR}/${SAMPLES_DIR_NAME}"
        fi
        save_best_schedule_result ${BEST_SCHEDULES_DIR} ${SAMPLES_DIR}
    done

    print_best_schedule_times $(dirname $0)/best
    exit
}

trap ctrl_c INT

if [ -z $APP ]; then
    APPS="bgu bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate conv_layer cuda_mat_mul iir_blur_generator depthwise_separable_conv"
else
    APPS=$APP
fi

NUM_APPS=0
for app in $APPS; do
    NUM_APPS=$((NUM_APPS + 1))
done

echo "Autotuning on $APPS for $MAX_ITERATIONS iteration(s)"

for app in $APPS; do
    SECONDS=0
    APP_DIR="${HALIDE_ROOT}/apps/${app}"

    LATEST_SAMPLES_DIR=$(ls -ld $APP_DIR/${DEFAULT_SAMPLES_DIR_NAME}* | tail -n 1 | rev | cut -d" " -f 1 | rev)
    if [[ ${RESUME} -eq 1 && -d ${LATEST_SAMPLES_DIR} ]]; then
        SAMPLES_DIR=${LATEST_SAMPLES_DIR}
        echo "Resuming from existing run: ${SAMPLES_DIR}"
    else
        SAMPLES_DIR_NAME=${DEFAULT_SAMPLES_DIR_NAME}-${CURRENT_DATE_TIME}
        SAMPLES_DIR="${APP_DIR}/${SAMPLES_DIR_NAME}"
        echo "Starting new run in: ${SAMPLES_DIR}"
    fi

    OUTPUT_FILE="${SAMPLES_DIR}/autotune_out.txt"
    PREDICTIONS_FILE="${SAMPLES_DIR}/predictions"
    PREDICTIONS_WITH_FILENAMES_FILE="${SAMPLES_DIR}/predictions_with_filenames"
    OUTLIERS_FILE="${SAMPLES_DIR}/outliers"
    BEST_TIMES_FILE="${SAMPLES_DIR}/best_times"

    mkdir -p ${SAMPLES_DIR}
    touch ${OUTPUT_FILE}

    ITERATION=1

    if [[ $PREDICT_ONLY != 1 ]]; then
        while [[ DONE -ne 1 ]]; do
            TRAIN_ONLY=${TRAIN_ONLY} SAMPLES_DIR=${SAMPLES_DIR} make -C ${APP_DIR} autotune | tee -a ${OUTPUT_FILE}

            if [[ $ITERATION -ge $MAX_ITERATIONS ]]; then
                break
            fi

            ITERATION=$((ITERATION + 1))
        done
    fi

    WEIGHTS_FILE="${SAMPLES_DIR}/updated.weights"
    predict_all ${HALIDE_ROOT} ${SAMPLES_DIR} ${WEIGHTS_FILE} ${PREDICTIONS_WITH_FILENAMES_FILE} 1
    awk -F", " '{printf("%f, %f\n", $2, $3);}' ${PREDICTIONS_WITH_FILENAMES_FILE} > ${PREDICTIONS_FILE}

    find_outliers ${PREDICTIONS_WITH_FILENAMES_FILE} ${OUTLIERS_FILE}

    extract_best_times ${HALIDE_ROOT} ${SAMPLES_DIR} ${BEST_TIMES_FILE}
    echo "Computing average statistics..."
    bash $(dirname $0)/../scripts/average_times.sh ${SAMPLES_DIR} >> ${OUTPUT_FILE}

    SCRIPTS_DIR="$(dirname $0)/../scripts"
    python3 ${SCRIPTS_DIR}/rsquared.py --predictions ${PREDICTIONS_FILE} >> ${OUTPUT_FILE}
    echo "Total autotune time (s): ${SECONDS}" >> ${OUTPUT_FILE}

    save_best_schedule_result ${BEST_SCHEDULES_DIR} ${SAMPLES_DIR}
    python3 ${SCRIPTS_DIR}/scatter.py --predictions ${PREDICTIONS_FILE} --app ${app} --output ${SAMPLES_DIR}

    if [[ $COMPARE_WITH_METRICS == 1 ]]; then
        echo "Comparing with metrics..."
        bash ${SCRIPTS_DIR}/compare_with_metrics.sh ${app} ${CURRENT_DATE_TIME} 5
    fi
done

print_best_schedule_times $(dirname $0)/best
