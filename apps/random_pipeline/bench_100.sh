#!/bin/bash

# Start a watchdog to kill any compilations that take too long
bash ./watchdog.sh &
WATCHDOG_PID=$!

function finish {
    kill $WATCHDOG_PID
}
trap finish EXIT

PIPELINES=100
SCHEDULES=100

# Build the shared things by building one pipeline
HL_TARGET=host-new_autoscheduler HL_SEED=root PIPELINE_SEED=0 HL_RANDOM_DROPOUT=50 HL_BEAM_SIZE=1 make build

for ((b=0;b<1000000;b++)); do
    echo Batch $b
    rm -f files_${b}.txt
    rm -f files_root_${b}.txt
    
    # Build lots of pipelines
    for ((p=0;p<$PIPELINES;p++)); do
	P=$((b * $PIPELINES + p))
	echo echo Building pipeline $P
	echo "HL_TARGET=host-new_autoscheduler HL_SEED=root PIPELINE_SEED=$P HL_RANDOM_DROPOUT=50 HL_BEAM_SIZE=1 make build 2>&1 | grep -v Nothing.to.be.done"
	for ((s=0;s<$SCHEDULES;s++)); do
            echo "HL_TARGET=host-new_autoscheduler HL_SEED=$s PIPELINE_SEED=$P HL_RANDOM_DROPOUT=50 HL_BEAM_SIZE=1 make build 2>&1 | grep -v Nothing.to.be.done"
	done
    done | xargs -P48 -I{} bash -c "{}"

    # Benchmark them
    for ((p=0;p<$PIPELINES;p++)); do
	P=$((b * $PIPELINES + p))
	echo Benchmarking pipeline $P
	F=bin/host-new_autoscheduler/pipeline_${P}/schedule_root_50_1/times.txt
	if [ ! -f $F ]; then HL_TARGET=host-new_autoscheduler HL_SEED=root PIPELINE_SEED=$P HL_RANDOM_DROPOUT=50 HL_BEAM_SIZE=1 make bench 2>&1 | grep -v "Nothing to be done"; fi

	grep '^Time' $F > /dev/null && echo $F >> files_root_${b}.txt
	for ((s=0;s<$SCHEDULES;s++)); do
	    F=bin/host-new_autoscheduler/pipeline_${P}/schedule_${s}_50_1/times.txt
            if [ ! -f $F ]; then HL_TARGET=host-new_autoscheduler HL_SEED=$s PIPELINE_SEED=$P HL_RANDOM_DROPOUT=50 HL_BEAM_SIZE=1 make bench 2>&1 | grep -v "Nothing to be done"; fi

	    grep '^Time' $F > /dev/null && echo $F >> files_${b}.txt
	done
    done

    # Extract the runtimes
    echo "Extracting runtimes..."
    cat files_${b}.txt | while read F; do grep '^Time' $F | cut -d: -f2 | cut -b2-; done > runtimes_${b}.txt

    # Extract the compute_root runtimes
    echo "Extracting compute_root runtimes..."
    cat files_root_${b}.txt | while read F; do grep '^Time' $F | cut -d: -f2 | cut -b2-; done > root_runtimes_${b}.txt

    # Extract the features
    echo "Extracting features..."
    cat files_${b}.txt | while read F; do echo $(grep '^YYY' ${F/times/stderr} | cut -d' ' -f2- | sort -n | cut -d' ' -f3-); done > features_${b}.txt

    # Extract the cost according to the hand-designed model (just the sum of the features)
    echo "Extracting costs..."
    cat files_${b}.txt | while read F; do echo $(grep '^State with cost' ${F/times/stderr} | cut -d' ' -f4 | cut -d: -f1); done  > costs_${b}.txt
done
