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

GREEDY_SAMPLES=$(find ${SAMPLES_DIR} -regextype sed -regex ".*/.*/[1-9][0-9]*/compile_err.txt")

grep "Number of states added:" ${SAMPLES_DIR}/*/0/compile_err.txt | awk -F" " '{sum += $5}; END{printf("Average number of states added (beam search): %d\n", sum / NR);}'

echo "$GREEDY_SAMPLES" | xargs grep "Number of featurizations computed:" | awk -F" " '{sum += $5}; END{printf("Average number of featurizations computed (greedy): %d\n", sum / NR);}'

grep "Number of featurizations computed:" ${SAMPLES_DIR}/*/0/compile_err.txt | awk -F" " '{sum += $5}; END{printf("Average number of featurizations computed (beam search): %d\n", sum / NR);}'

echo "$GREEDY_SAMPLES" | xargs grep "Number of schedules evaluated by cost model" | awk -F" " '{sum += $8}; END{printf("Average number of schedules evaluated by cost model (greedy): %d\n", sum / NR);}'

grep "Number of schedules evaluated by cost model" ${SAMPLES_DIR}/*/0/compile_err.txt | awk -F" " '{sum += $8}; END{printf("Average number of schedules evaluated by cost model (beam search): %d\n", sum / NR);}'

echo "$GREEDY_SAMPLES" | xargs grep "Compile time" | awk '{sum += $4}; END{printf("Average greedy compile time: %f s\n", sum / NR);}'

grep "Compile time" ${SAMPLES_DIR}/*/0/compile_err.txt | awk -F" " '{sum += $4}; END{printf("Average beam search compile time: %f s\n", sum / NR);}'

echo "$GREEDY_SAMPLES" | xargs grep "Time taken for autoscheduler" | awk '{sum += $6}; END{printf("Average greedy autoschedule time: %f s\n", sum / NR);}'

grep "Time taken for autoscheduler" ${SAMPLES_DIR}/*/0/compile_err.txt | awk -F" " '{sum += $6}; END{printf("Average beam search autoschedule time: %f s\n", sum / NR);}'

# Average featurization time
echo "$GREEDY_SAMPLES" | xargs grep "Average featurization time" | awk -F" " '{sum += $NF}; END{printf("Average featurization time (greedy): %f\n", sum / NR);}'

grep "Average featurization time" ${SAMPLES_DIR}/*/0/compile_err.txt | awk -F" " '{sum += $NF}; END{printf("Average featurization time (beam search): %f\n", sum / NR);}'

# Average cost model evaluation time
echo "$GREEDY_SAMPLES" | xargs grep "Average cost model evaluation time" | awk -F" " '{sum += $NF}; END{printf("Average cost model evaluation time (greedy): %f\n", sum / NR);}'

grep "Average cost model evaluation time" ${SAMPLES_DIR}/*/0/compile_err.txt | awk -F" " '{sum += $NF}; END{printf("Average cost model evaluation time (beam search): %f\n", sum / NR);}'

# Average number of memoization hits
echo "$GREEDY_SAMPLES" | xargs grep "Number of memoization hits:" | awk -F" " '{sum += $NF}; END{printf("Average number of memoization hits (greedy): %d\n", sum / NR);}'

grep "Number of memoization hits:" ${SAMPLES_DIR}/*/0/compile_err.txt | awk -F" " '{sum += $NF}; END{printf("Average number of memoization hits (beam search): %d\n", sum / NR);}'

# Average number of memoization misses
echo "$GREEDY_SAMPLES" | xargs grep "Number of memoization misses:" | awk -F" " '{sum += $NF}; END{printf("Average number of memoization misses (greedy): %d\n", sum / NR);}'

grep "Number of memoization misses:" ${SAMPLES_DIR}/*/0/compile_err.txt | awk -F" " '{sum += $NF}; END{printf("Average number of memoization misses (beam search): %d\n", sum / NR);}'

echo "$GREEDY_SAMPLES" | xargs grep "Number of tilings generated:" | awk -F" " '{sum += $NF}; END{printf("Average number of tilings generated (greedy): %d\n", sum / NR);}'

grep "Number of tilings generated:" ${SAMPLES_DIR}/*/0/compile_err.txt | awk -F" " '{sum += $NF}; END{printf("Average number of tilings generated (beam search): %d\n", sum / NR);}'

echo "$GREEDY_SAMPLES" | xargs grep "Number of tilings accepted:" | awk -F" " '{sum += $NF}; END{printf("Average number of tilings accepted (greedy): %d\n", sum / NR);}'

grep "Number of tilings accepted:" ${SAMPLES_DIR}/*/0/compile_err.txt | awk -F" " '{sum += $NF}; END{printf("Average number of tilings accepted (beam search): %d\n", sum / NR);}'

echo
