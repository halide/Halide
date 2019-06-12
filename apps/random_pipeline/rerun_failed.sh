#!/bin/bash

# Re-run a failed command
# Commands:
# a => autoschedule
# c => compile
# b => benchmark
# au => augment
if [ $# -ne 3 ]; then
  echo "Usage: $0 batch_id sample_id command"
  exit
fi

BATCH_ID=${1}
SAMPLE_ID=${2}
COMMAND=${3}

if [ $COMMAND == "a" ]; then
    COMMAND="autoschedule"
elif [ $COMMAND == "c" ]; then
    COMMAND="compilation"
elif [ $COMMAND == "b" ]; then
    COMMAND="benchmark"
elif [ $COMMAND == "au" ]; then
    COMMAND="augment"
else
    echo "Unknown command: ${COMMAND}"
    exit
fi

DIR=samples/batch_${BATCH_ID}/${SAMPLE_ID}

if [[ ! -d $DIR ]]; then
    echo "Sample does not exist: ${DIR}"
    exit
fi

COMMAND_FILE=${DIR}/${COMMAND}_command.txt
if [[ ! -f $COMMAND_FILE ]]; then
    echo "Command file does not exist: ${COMMAND_FILE}"
    exit
fi

bash ${COMMAND_FILE}
