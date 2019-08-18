#!/bin/bash

if [ $# -ne 4 ]; then
  echo "Usage: $0 samples_dir weights_dir autoschedule_bin_dir predictions_file"
  exit
fi

echo $(cd $(dirname $0); pwd)

SAMPLES_DIR=${1}
WEIGHTS_DIR=${2}
AUTOSCHED_BIN=${3}
PREDICTIONS_FILE=${4}

NUM_CORES=80

make ../autoscheduler/bin/train_cost_model

find ${SAMPLES_DIR} | grep sample$ | PREDICTIONS_FILE=${PREDICTIONS_FILE} HL_NUM_THREADS=${NUM_CORES} HL_WEIGHTS_DIR=${WEIGHTS_DIR} ${AUTOSCHED_BIN}/train_cost_model 1 0.0001
