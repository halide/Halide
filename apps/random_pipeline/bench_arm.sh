#!/bin/bash

# Let the ftp server know we've started
IP=$( ifconfig eth0 | grep 192.168 | cut -d: -f2 | cut -d' ' -f1 )
touch ___started.${IP}.txt
bash ./ftp_up.sh ___started.${IP}.txt
rm ___started.${IP}.txt

# Start a watchdog to kill any compilations that take too long
bash ./watchdog.sh &
WATCHDOG_PID=$!

function finish {
    kill $WATCHDOG_PID
}
trap finish EXIT

mkdir -p results

PIPELINES=5
SCHEDULES=100

# Build the shared things by building one pipeline
HL_TARGET=host-new_autoscheduler HL_SEED=root PIPELINE_SEED=0 HL_RANDOM_DROPOUT=50 HL_BEAM_SIZE=1 make build

for ((i=0;i<10000000;i++)); do
    b=$RANDOM
    echo Batch $b
    SUFFIX=_${IP}_${b}
    rm -f results/files${SUFFIX}.txt
    rm -f results/files_root${SUFFIX}.txt

    # Build lots of pipelines
    for ((p=0;p<$PIPELINES;p++)); do
	P=$((b * $PIPELINES + p))
	echo echo Building pipeline $P
	echo "HL_TARGET=host-new_autoscheduler HL_SEED=root PIPELINE_SEED=$P HL_RANDOM_DROPOUT=50 HL_BEAM_SIZE=1 make build 2>&1 | grep -v Nothing.to.be.done"
	for ((s=0;s<$SCHEDULES;s++)); do
            echo "HL_TARGET=host-new_autoscheduler HL_SEED=$s PIPELINE_SEED=$P HL_RANDOM_DROPOUT=50 HL_BEAM_SIZE=1 make build 2>&1 | grep -v Nothing.to.be.done"
	done
    done | xargs -P4 -I{} bash -c "{}"

    # Benchmark them
    for ((p=0;p<$PIPELINES;p++)); do
	P=$((b * $PIPELINES + p))
	echo Benchmarking pipeline $P
	F=bin/host-new_autoscheduler/pipeline_${P}/schedule_root_50_1/times.txt
	if [ ! -f $F ]; then HL_TARGET=host-new_autoscheduler HL_SEED=root PIPELINE_SEED=$P HL_RANDOM_DROPOUT=50 HL_BEAM_SIZE=1 make bench 2>&1 | grep -v "Nothing to be done"; fi

	grep '^Time' $F > /dev/null && echo $F >> results/files_root${SUFFIX}.txt
	for ((s=0;s<$SCHEDULES;s++)); do
	    F=bin/host-new_autoscheduler/pipeline_${P}/schedule_${s}_50_1/times.txt
            if [ ! -f $F ]; then HL_TARGET=host-new_autoscheduler HL_SEED=$s PIPELINE_SEED=$P HL_RANDOM_DROPOUT=50 HL_BEAM_SIZE=1 make bench 2>&1 | grep -v "Nothing to be done"; fi

	    grep '^Time' $F > /dev/null && echo $F >> results/files${SUFFIX}.txt
	done
    done

    # Extract the runtimes
    echo "Extracting runtimes..."
    cat results/files${SUFFIX}.txt | while read F; do grep '^Time' $F | cut -d: -f2 | cut -b2-; done > results/runtimes${SUFFIX}.txt

    # Extract the number of malloc calls
    echo "Extracting mallocs..."
    cat results/files${SUFFIX}.txt | while read F; do grep '^Malloc' $F | cut -d: -f2 | cut -b2-; done > results/mallocs${SUFFIX}.txt    

    # Extract the compute_root runtimes
    echo "Extracting compute_root runtimes..."
    cat results/files_root${SUFFIX}.txt | while read F; do grep '^Time' $F | cut -d: -f2 | cut -b2-; done > results/root_runtimes${SUFFIX}.txt

    # Extract the features
    echo "Extracting features..."
    cat results/files${SUFFIX}.txt | while read F; do echo $(grep '^YYY' ${F/times/stderr} | cut -d' ' -f2- | sort -n | cut -d' ' -f3-); done > results/features${SUFFIX}.txt

    # Extract failed proofs
    echo "Extracting any failed proofs..."
    cat results/files${SUFFIX}.txt | while read F; do grep -A1 'Failed to prove' ${F/times/stderr}; done > results/failed_proofs${SUFFIX}.txt    

    # Extract the cost according to the hand-designed model (just the sum of the features)
    echo "Extracting costs..."
    cat results/files${SUFFIX}.txt | while read F; do echo $(grep '^State with cost' ${F/times/stderr} | cut -d' ' -f4 | cut -d: -f1); done  > results/costs${SUFFIX}.txt

    chmod a+x ftp_up.sh
    ls results/*${SUFFIX}.txt | xargs -n1 ./ftp_up.sh
done
