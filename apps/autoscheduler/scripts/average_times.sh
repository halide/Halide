#!/bin/bash

if [ $# -ne 1 ]; then
  echo "Usage: $0 samples_dir"
  exit
fi

source $(dirname $0)/utils.sh

find_halide HALIDE_ROOT

make_dir_path_absolute ${1} SAMPLES_DIR

echo
echo "Samples directory: ${SAMPLES_DIR}"

find ${SAMPLES_DIR} | grep "compile_err.txt" | xargs grep "Average featurization time" | awk -F" " '{sum += $5}; END{printf("Average featurization time: %f ms\n", sum / NR)}'

find ${SAMPLES_DIR} | grep "compile_err.txt" | xargs grep "Average cost model evaluation time" | awk -F" " '{sum += $7}; END{printf("Average cost model evaluation time: %f ms\n", sum / NR)}'
