#!/bin/bash

if [ $# -ne 2 ]; then
    echo "Usage: $0 samples_dir app"
    exit
fi

set -e

source $(dirname $0)/utils.sh

SCRIPTS_DIR="$(dirname $0)"

find_halide HALIDE_ROOT

SAMPLES_DIR=${1}
app=${2}

APP_DIR="${HALIDE_ROOT}/apps/${app}/${SAMPLES_DIR}"

echo $app

for batch_dir in ${APP_DIR}/batch_*; do
  echo "Processing ${batch_dir}"

  PREDICTIONS_FILENAME="${batch_dir}/app_predictions"
  FORMATTED_FILENAME="${batch_dir}/formatted_app_predictions"

  rm -f ${PREDICTIONS_FILENAME}
  rm -f ${FORMATTED_FILENAME}

  SAMPLES=$(find ${batch_dir} | grep bench.txt | xargs dirname | sort)

  for S in ${SAMPLES}; do
    store_bytes=$(grep "bytes spill loads" "${S}/compile_err.txt" | cut -d"," -f 2 | cut -d" " -f 2 | paste -sd+ - | bc)
    load_bytes=$(grep "bytes spill loads" "${S}/compile_err.txt" | cut -d"," -f 3 | cut -d" " -f 2 | paste -sd+ - | bc)

    total_bytes=0
    if [[ -z ${store_bytes} ]]; then
      store_bytes=0
    fi
    if [[ -z ${load_bytes} ]]; then
      load_bytes=0
    fi
    total_bytes=$(echo "${load_bytes} + ${store_bytes}" | bc)

    ACTUAL=$(grep "best case" "${S}/bench.txt" | cut -d" " -f 8)
    PREDICTED=$(grep "Best cost" "${S}/compile_err.txt" | cut -d" " -f 3)
    PREDICTED=$(grep "Best cost" "${S}/compile_err.txt" | cut -d" " -f 3)
    ID=$(basename ${S})
    echo $app,$ID,$PREDICTED,$ACTUAL,$total_bytes >> ${PREDICTIONS_FILENAME}
  done

  python3 ${SCRIPTS_DIR}/app_predictions.py --filename ${PREDICTIONS_FILENAME} --app ${app} >> ${FORMATTED_FILENAME}
done

rm -f ${APP_DIR}/formatted_app_predictions
paste -d , ${APP_DIR}/batch_*/formatted_app_predictions >> ${APP_DIR}/formatted_app_predictions


