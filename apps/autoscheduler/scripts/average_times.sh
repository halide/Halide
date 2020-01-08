#!/bin/bash

if [ $# -ne 1 ]; then
  echo "Usage: $0 samples_dir"
  exit
fi

SAMPLES_DIR=${1}

if [ ! -d ${SAMPLES_DIR} ]; then
    echo "Samples directory not found: ${SAMPLES_DIR}"
    exit
fi

echo "Samples directory: ${SAMPLES_DIR}"

grep "Number of featurizations computed:" ${SAMPLES_DIR}/*/*/compile_err.txt | awk -F" " '$1 !~ /\/0\/compile_err.txt:Number$/ {sum += $5}; END{printf("Average number of featurizations computed (greedy): %f\n", sum / NR);}'

grep "Number of featurizations computed:" ${SAMPLES_DIR}/*/0/compile_err.txt | awk -F" " '{sum += $5}; END{printf("Average number of featurizations computed (beam search): %f\n", sum / NR);}'

grep "Number of memoized featurizations:" ${SAMPLES_DIR}/*/*/compile_err.txt | awk -F" " '$1 !~ /\/0\/compile_err.txt:Number$/ {sum += $5}; END{printf("Average number of memoized featurizations (greedy): %f\n", sum / NR);}'

grep "Number of memoized featurizations:" ${SAMPLES_DIR}/*/0/compile_err.txt | awk -F" " '{sum += $5}; END{printf("Average number of memoized featurizations (beam search): %f\n", sum / NR);}'

grep "Compile time" ${SAMPLES_DIR}/*/*/compile_err.txt | awk '$1 !~ /\/0\/compile_err.txt:Compile$/ {sum += $4}; END{printf("Average greedy compile time: %f\n", sum / NR);}'

grep "Compile time" ${SAMPLES_DIR}/*/0/compile_err.txt | awk -F" " '{sum += $4}; END{printf("Average beam search compile time: %f\n", sum / NR);}'

find ${SAMPLES_DIR} | grep "compile_err.txt" | xargs grep "Average featurization time" | awk -F" " '{sum += $5}; END{printf("Average featurization time: %f ms\n", sum / NR);}'

find ${SAMPLES_DIR} | grep "compile_err.txt" | xargs grep "Average cost model evaluation time" | awk -F" " '{sum += $7}; END{printf("Average cost model evaluation time: %f ms\n", sum / NR);}'

echo
