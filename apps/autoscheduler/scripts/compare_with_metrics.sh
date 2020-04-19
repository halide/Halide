#!/bin/bash

if [ $# -ne 2 ]; then
    echo "Usage: $0 outliers_file num"
    exit
fi

source $(dirname $0)/utils.sh

find_halide HALIDE_ROOT

trap exit INT

OUTLIERS_FILE=${1}
NUM=${2}

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
