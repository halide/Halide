#!/bin/bash

# Build lots of pipelines
for ((p=0;p<30;p++)); do
    for ((s=0;s<30;s++)); do
        HL_TARGET=host-new_autoscheduler HL_SEED=$s PIPELINE_SEED=$p HL_RANDOM_DROPOUT=50 HL_BEAM_SIZE=1 make build &
    done
    wait
done

# Benchmark them

for ((p=0;p<30;p++)); do
    for ((s=0;s<30;s++)); do
        HL_TARGET=host-new_autoscheduler HL_SEED=$s PIPELINE_SEED=$p HL_RANDOM_DROPOUT=50 HL_BEAM_SIZE=1 make bench
    done
done

# Find the successful runs
find bin -name times.txt | while read F; do grep 'Auto-scheduled time' $F >/dev/null && echo $F; done > files.txt

# Extract the runtimes
cat files.txt | while read F; do grep 'Auto-scheduled time' $F | cut -d: -f2 | cut -dm -f1 | cut -b2-; done > runtimes.txt

# Extract the features
cat files.txt | while read F; do echo $(grep '^XXX' ${F/times/stderr} | cut -d' ' -f2); done > features.txt

# Extract the cost according to the hand-designed model (just the sum of the features)
cat files.txt | while read F; do echo $(grep '^State with cost' ${F/times/stderr} | cut -d' ' -f4 | cut -d: -f1); done  > costs.txt
