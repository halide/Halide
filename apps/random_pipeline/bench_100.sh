#!/bin/bash

PIPELINES=5
SCHEDULES=5

# Build the shared things by building one pipeline
HL_TARGET=host-new_autoscheduler HL_SEED=root PIPELINE_SEED=0 HL_RANDOM_DROPOUT=50 HL_BEAM_SIZE=1 make build &

# Build lots of pipelines
for ((p=0;p<$PIPELINES;p++)); do
    HL_TARGET=host-new_autoscheduler HL_SEED=root PIPELINE_SEED=$p HL_RANDOM_DROPOUT=50 HL_BEAM_SIZE=1 make build &
    for ((s=0;s<$SCHEDULES;s++)); do
        HL_TARGET=host-new_autoscheduler HL_SEED=$s PIPELINE_SEED=$p HL_RANDOM_DROPOUT=50 HL_BEAM_SIZE=1 make build &
    done
    wait
done

# Benchmark them
for ((p=0;p<$PIPELINES;p++)); do
    HL_TARGET=host-new_autoscheduler HL_SEED=root PIPELINE_SEED=$p HL_RANDOM_DROPOUT=50 HL_BEAM_SIZE=1 make bench
    for ((s=0;s<$SCHEDULES;s++)); do
        HL_TARGET=host-new_autoscheduler HL_SEED=$s PIPELINE_SEED=$p HL_RANDOM_DROPOUT=50 HL_BEAM_SIZE=1 make bench
    done
done

# Find the successful runs
find bin -name times.txt | grep -v "_root_" | while read F; do grep '^Time' $F >/dev/null && echo $F; done > files.txt

# Extract the runtimes
cat files.txt | while read F; do grep '^Time' $F | cut -d: -f2 | cut -b2-; done > runtimes.txt

# Extract the compute_root runtimes
find bin -name times.txt | grep "_root_" | while read F; do grep '^Time' $F | cut -d: -f2 | cut -b2-; done > root_runtimes.txt

# Extract the features
cat files.txt | while read F; do echo $(grep '^XXX' ${F/times/stderr} | cut -d' ' -f2); done > features.txt

# Extract the features
cat files.txt | while read F; do echo $(grep '^YYY' ${F/times/stderr} | sort | cut -d' ' -f2); done > features_2.txt

# Extract the cost according to the hand-designed model (just the sum of the features)
cat files.txt | while read F; do echo $(grep '^State with cost' ${F/times/stderr} | cut -d' ' -f4 | cut -d: -f1); done  > costs.txt
