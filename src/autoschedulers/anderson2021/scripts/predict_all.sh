#!/bin/bash

if [ $# -ne 7 ]; then
  echo "Usage: $0 halide_build_dir samples_dir weights_file predictions_file include_filenames limit parallelism"
  exit
fi

SCRIPTS_DIR=$(dirname $0)
source ${SCRIPTS_DIR}/utils.sh

HALIDE_BUILD_DIR=${1}
make_dir_path_absolute ${2} SAMPLES_DIR
make_file_path_absolute ${3} WEIGHTS_FILE
make_file_path_absolute ${4} PREDICTIONS_FILE
INCLUDE_FILENAMES=${5}
LIMIT=${6}
PARALLELISM=${7}

echo
echo "Samples directory: ${SAMPLES_DIR}"
echo "Weights file: ${WEIGHTS_FILE}"
echo "Saving predictions to: ${PREDICTIONS_FILE}"

NUM_EPOCHS=1

retrain_cost_model ${HALIDE_BUILD_DIR} ${SAMPLES_DIR} ${WEIGHTS_FILE} ${PARALLELISM} ${NUM_EPOCHS} 0 0.001 ${PREDICTIONS_FILE} 0 0 ${LIMIT}

if [[ $INCLUDE_FILENAMES == 1 ]]; then
  exit
fi

RESULT=$(cat ${PREDICTIONS_FILE} | awk -F", " '{printf("%f, %f\n", $2, $3);}') > ${PREDICTIONS_FILE}
echo "$RESULT" > ${PREDICTIONS_FILE}
RESULT=$(cat ${PREDICTIONS_FILE}_validation_set | awk -F", " '{printf("%f, %f\n", $2, $3);}') > ${PREDICTIONS_FILE}_validation_set
echo "$RESULT" > ${PREDICTIONS_FILE}_validation_set
