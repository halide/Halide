#!/bin/bash

if [ $# -ne 2 ]; then
    echo "Usage: $0 dir output_file"
    exit
fi

DIR=${1}
OUTPUT_FILE=${2}
FILENAME=${DIR}/autotune_out.txt

if [[ ! -f ${FILENAME} ]]; then
    echo "${FILENAME} does not exist"
    exit
fi

grep "Best runtime" ${FILENAME} | cut -d" " -f 4 | cut -d"," -f 1 > ${OUTPUT_FILE}
echo "Best run times saved to: ${OUTPUT_FILE}"
