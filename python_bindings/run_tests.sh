#!/bin/bash

#set -x # print commands
#PYTHON=echo
PYTHON=python3

FAILED=0

# separator
S=" --------- "

for i in apps/*.py
do
  echo $S $PYTHON $i $S
  $PYTHON $i
  if [[ "$?" -ne "0" ]]; then
	let FAILED=1
	break
  fi
done

if [[ "$FAILED" -ne "0" ]]; then
  echo "App failed"
  exit -1
fi

for i in tutorial/*.py
do
  echo $S $PYTHON $i $S
  $PYTHON $i
  if [[ "$?" -ne "0" ]]; then
	let FAILED=1
	break
  fi
done

if [[ "$FAILED" -ne "0" ]]; then
  echo "lesson failed"
  exit -1
fi

