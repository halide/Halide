#!/bin/bash

if [ $# -ne 2 ]; then
    echo "Usage: $0 samples_dir num"
    exit
fi

source $(dirname $0)/utils.sh

SAMPLES_DIR=${1}
NUM=${2}

reautoschedule $SAMPLES_DIR $NUM
