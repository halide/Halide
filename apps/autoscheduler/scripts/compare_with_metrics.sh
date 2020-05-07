#!/bin/bash

if [ $# -ne 3 ]; then
    echo "Usage: $0 app timestamp num"
    exit
fi

source $(dirname $0)/utils.sh

find_halide HALIDE_ROOT

trap exit INT

APP=${1}
TIMESTAMP=${2}
NUM=${3}

OUTLIERS_FILE="${HALIDE_ROOT}/apps/${APP}/autotuned_samples-${TIMESTAMP}/outliers"

if [ ! -f ${OUTLIERS_FILE} ]; then
    echo "${OUTLIERS_FILE} not found."
    exit
fi

if [[ $NUM == 0 ]]; then
    FILES=$(cat "${OUTLIERS_FILE}" | awk -F", " '{print $1}')

    for f in ${FILES}; do
        compare_with_profiler ${HALIDE_ROOT} $(dirname "${f}")
    done

    python3 $(dirname $0)/compare_with_metrics.py --outliers "${OUTLIERS_FILE}" --N "${NUM}" | tee -a "$(dirname ${OUTLIERS_FILE})/metrics_comparisons"
    exit
fi

FILES=$(cat "${OUTLIERS_FILE}" | head -n "${NUM}" | awk -F", " '{print $1}')

for f in ${FILES}; do
    compare_with_profiler ${HALIDE_ROOT} $(dirname "${f}")
done

FILES=$(cat "${OUTLIERS_FILE}" | tail -n "${NUM}" | awk -F", " '{print $1}')

for f in ${FILES}; do
    compare_with_profiler ${HALIDE_ROOT} $(dirname "${f}")
done

python3 $(dirname $0)/compare_with_metrics.py --outliers "${OUTLIERS_FILE}" --N "${NUM}" | tee -a "$(dirname ${OUTLIERS_FILE})/metrics_comparisons"
