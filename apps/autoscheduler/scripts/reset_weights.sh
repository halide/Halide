#!/bin/bash

if [ $# -ne 1 ]; then
  echo "Usage: $0 weights_file"
  exit
fi

source $(dirname $0)/../scripts/utils.sh

find_halide HALIDE_ROOT

build_retrain_cost_model ${HALIDE_ROOT}

WEIGHTS_FILE=${1}

reset_weights ${HALIDE_ROOT} ${WEIGHTS_FILE}
