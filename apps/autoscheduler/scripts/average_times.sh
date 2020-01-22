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

find ${SAMPLES_DIR} -regextype sed -regex ".*/.*/[1-9][0-9]*/compile_err.txt" | xargs grep "Number of states added:" | awk -F" " '{sum += $5}; END{printf("Average number of states added (greedy): %f\n", sum / NR);}'

grep "Number of states added:" ${SAMPLES_DIR}/*/0/compile_err.txt | awk -F" " '{sum += $5}; END{printf("Average number of states added (beam search): %f\n", sum / NR);}'

find ${SAMPLES_DIR} -regextype sed -regex ".*/.*/[1-9][0-9]*/compile_err.txt" | xargs grep "Number of featurizations computed:" | awk -F" " '{sum += $5}; END{printf("Average number of featurizations computed (greedy): %f\n", sum / NR);}'

grep "Number of featurizations computed:" ${SAMPLES_DIR}/*/0/compile_err.txt | awk -F" " '{sum += $5}; END{printf("Average number of featurizations computed (beam search): %f\n", sum / NR);}'

find ${SAMPLES_DIR} -regextype sed -regex ".*/.*/[1-9][0-9]*/compile_err.txt" | xargs grep "Number of schedules evaluated by cost model" | awk -F" " '{sum += $8}; END{printf("Average number of schedules evaluated by cost model (greedy): %f\n", sum / NR);}'

grep "Number of schedules evaluated by cost model" ${SAMPLES_DIR}/*/0/compile_err.txt | awk -F" " '{sum += $8}; END{printf("Average number of schedules evaluated by cost model (beam search): %f\n", sum / NR);}'

find ${SAMPLES_DIR} -regextype sed -regex ".*/.*/[1-9][0-9]*/compile_err.txt" | xargs grep "Compile time" | awk '{sum += $4}; END{printf("Average greedy compile time: %f s\n", sum / NR);}'

grep "Compile time" ${SAMPLES_DIR}/*/0/compile_err.txt | awk -F" " '{sum += $4}; END{printf("Average beam search compile time: %f s\n", sum / NR);}'

find ${SAMPLES_DIR} -regextype sed -regex ".*/.*/[1-9][0-9]*/compile_err.txt" | xargs grep "Time taken for autoscheduler" | awk '{sum += $6}; END{printf("Average greedy autoschedule time: %f s\n", sum / NR);}'

grep "Time taken for autoscheduler" ${SAMPLES_DIR}/*/0/compile_err.txt | awk -F" " '{sum += $6}; END{printf("Average beam search autoschedule time: %f s\n", sum / NR);}'

find ${SAMPLES_DIR} | grep "compile_err.txt" | xargs grep "Average featurization time" | awk -F" " '{sum += $5}; END{printf("Average featurization time: %f ms\n", sum / NR);}'

find ${SAMPLES_DIR} | grep "compile_err.txt" | xargs grep "Average cost model evaluation time" | awk -F" " '{sum += $7}; END{printf("Average cost model evaluation time: %f ms\n", sum / NR);}'

find ${SAMPLES_DIR} -regextype sed -regex ".*/.*/[1-9][0-9]*/compile_err.txt" | xargs grep "Number of memoized featurizations:" | awk -F" " '{sum += $5}; END{printf("Average number of memoized featurizations (greedy): %f\n", sum / NR);}'

grep "Number of memoized featurizations:" ${SAMPLES_DIR}/*/0/compile_err.txt | awk -F" " '{sum += $5}; END{printf("Average number of memoized featurizations (beam search): %f\n", sum / NR);}'

echo
