#!/bin/bash

#set -x # print commands
#PYTHON=echo
PYTHON=python3

# Operate in the build directory, so that output files don't pollute the top-level directory.
cd build

BUILDPATH="." # Relative to the build dir. Adjust as needed.
export PYTHONPATH="$BUILDPATH:$PYTHONPATH"
echo "PYTHONPATH ==" $PYTHONPATH

FAILED=0

# separator
S=" --------- "
Sa=" >>>>>>>> "
Sb=" <<<<<<<< "

# skipping lesson_10_aot_compilation_run.py because it
# needs a manual compilation step before running

TUTORIALS=(
tutorial/lesson_01_basics.py
tutorial/lesson_02_input_image.py
tutorial/lesson_03_debugging_1.py
tutorial/lesson_04_debugging_2.py
tutorial/lesson_05_scheduling_1.py
tutorial/lesson_06_realizing_over_shifted_domains.py
tutorial/lesson_07_multi_stage_pipelines.py
tutorial/lesson_08_scheduling_2.py
tutorial/lesson_09_update_definitions.py
tutorial/lesson_10_aot_compilation_generate.py
tutorial/lesson_11_cross_compilation.py
tutorial/lesson_12_using_the_gpu.py
tutorial/lesson_13_tuples.py
tutorial/lesson_14_types.py
)

if [ -z "$ROOT_DIR" ]; then ROOT_DIR=$(dirname $0); fi

for i in ${TUTORIALS[*]}
do
  echo $S $PYTHON $i $S
  $PYTHON ${ROOT_DIR}/$i
  if [[ "$?" -ne "0" ]]; then
        echo "$Sa Lesson failed $Sb"
	let FAILED=1
	break
  fi
done

if [[ "$FAILED" -ne "0" ]]; then
  exit -1
else
  echo "$S (all lessons ran, (skipped lesson_10b)) $S"
fi
