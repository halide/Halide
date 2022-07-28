#!/bin/bash

if [ $# -ne 5 ]; then
  echo "Usage: $0 samples_dir weights_file predictions_file include_filenames limit"
  exit
fi

source $(dirname $0)/utils.sh

find_halide HALIDE_ROOT

make_dir_path_absolute ${1} SAMPLES_DIR
make_file_path_absolute ${2} WEIGHTS_FILE
make_file_path_absolute ${3} PREDICTIONS_FILE
INCLUDE_FILENAMES=${4}
LIMIT=${5}

echo
echo "Samples directory: ${SAMPLES_DIR}"
echo "Weights file: ${WEIGHTS_FILE}"
echo "Saving predictions to: ${PREDICTIONS_FILE}"

build_retrain_cost_model ${HALIDE_ROOT}

NUM_CORES=80
NUM_EPOCHS=1

retrain_cost_model ${HALIDE_ROOT} ${SAMPLES_DIR} ${WEIGHTS_FILE} ${NUM_CORES} ${NUM_EPOCHS} 0 0.001 ${PREDICTIONS_FILE} 0 0 ${LIMIT}

if [[ $INCLUDE_FILENAMES == 1 ]]; then
  exit
fi

RESULT=$(cat ${PREDICTIONS_FILE} | awk -F", " '{printf("%f, %f\n", $2, $3);}') > ${PREDICTIONS_FILE}
echo "$RESULT" > ${PREDICTIONS_FILE}
RESULT=$(cat ${PREDICTIONS_FILE}_validation_set | awk -F", " '{printf("%f, %f\n", $2, $3);}') > ${PREDICTIONS_FILE}_validation_set
echo "$RESULT" > ${PREDICTIONS_FILE}_validation_set
