#!/bin/bash

# Re-run a failed command
# Commands:
# a => autoschedule
# ad => autoschedule with debug target
# c => compile
# b => benchmark
# au => augment
# p => profile
if [ $# -ne 5 ]; then
  echo "Usage: $0 dir batch_id extra_generator_args_id sample_id command"
  exit
fi

DIR=${1}
BATCH_ID=${2}
EXTRA_GENERATOR_ARGS_ID=${3}
SAMPLE_ID=${4}
COMMAND=${5}

DEBUG=0

if [ $COMMAND == "a" ]; then
    COMMAND="autoschedule"
elif [ $COMMAND == "ad" ]; then
    DEBUG=1
    COMMAND="autoschedule"
elif [ $COMMAND == "c" ]; then
    COMMAND="compile"
elif [ $COMMAND == "b" ]; then
    COMMAND="benchmark"
elif [ $COMMAND == "au" ]; then
    COMMAND="augment"
elif [ $COMMAND == "p" ]; then
    COMMAND="nvprof"
else
    echo "Unknown command: ${COMMAND}"
    exit
fi

if [[ $EXTRA_GENERATOR_ARGS_ID -ge 0 ]]; then
    DIR=${DIR}/batch_${BATCH_ID}_${EXTRA_GENERATOR_ARGS_ID}/${SAMPLE_ID}
else
    DIR=${DIR}/batch_${BATCH_ID}/${SAMPLE_ID}
fi

if [[ ! -d $DIR ]]; then
    echo "Sample does not exist: ${DIR}"
    exit
fi

COMMAND_FILE=${DIR}/${COMMAND}_command.txt
if [[ ! -f $COMMAND_FILE ]]; then
    echo "Command file does not exist: ${COMMAND_FILE}"
    exit
fi

if [[ $DEBUG == 1 ]]; then
    CMD=$(cat ${COMMAND_FILE} | sed "s/host-cuda/host-cuda-debug/g")
else
    CMD=$(cat ${COMMAND_FILE})
fi

echo "Running command in: ${COMMAND_FILE}"
echo ${CMD}
eval $CMD

