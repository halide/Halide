#!/bin/bash

#set -x # print commands
#PYTHON=echo
PYTHON=python3

FAILED=0

# separator
S=" --------- "
Sa=" >>>>>>>> "
Sb=" <<<<<<<< "



for i in apps/*.py
do
  echo $S $PYTHON $i $S
  $PYTHON $i
  if [[ "$?" -ne "0" ]]; then
        echo "$Sa App failed $Sb"
	let FAILED=1
	#break
  fi
done

if [[ "$FAILED" -ne "0" ]]; then
  exit -1
else
  echo "$S (all applications ran) $S"
fi

