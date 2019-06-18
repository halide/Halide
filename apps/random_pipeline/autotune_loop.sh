#!/bin/bash

# set -x

# mode => debug | train
# debug mode only generates data, uses -debug on HL_TARGET, and collects shared
# memory errors
# train mode generates data without -debug and trains the model
if [ $# -ne 1 ]; then
  echo "Usage: $0 mode [debug|train]"
  exit
fi

# Install a watchdog to kill benchmarking processes that take too long
bash ./watchdog_bench.sh &
WATCHDOG_PID=$!
function finish {
    kill $WATCHDOG_PID
}
trap finish EXIT

if [ ${1} == "debug" ]; then
    DEBUG_MODE=1
    echo "Running in debug mode"
elif [ ${1} == "train" ]; then
    DEBUG_MODE=0
    echo "Running in train mode"
else
    echo "Unknown mode"
    exit
fi

# Build the generator to autotune.
GENERATOR=./bin/random_pipeline.generator
PIPELINE=random_pipeline
make bin/random_pipeline.generator

SAMPLES=samples
# SAMPLES=/mnt/e/samples

# Build some tools we need.
make -C ../autoscheduler ../autoscheduler/bin/augment_sample
make -C ../autoscheduler ../autoscheduler/bin/train_cost_model
make -C ../autoscheduler ../autoscheduler/bin/libauto_schedule.so
cp ../autoscheduler/bin/augment_sample ../autoscheduler/bin/train_cost_model  ../autoscheduler/bin/libauto_schedule.so bin/

mkdir -p ${SAMPLES}

# A batch of this many samples is built in parallel, and then
# benchmarked serially. Set to number of cores.
BATCH_SIZE=32

WEIGHTS_DIR=../autoscheduler/gpu_weights
mkdir -p ${WEIGHTS_DIR}

MAX_STAGES=5

HL_TARGET=x86-64-avx2-disable_llvm_loop_vectorize-disable_llvm_loop_unroll-cuda

if [ $DEBUG_MODE == 1 ]; then
    HL_TARGET=${HL_TARGET}-debug
fi

record_failed() {
    BATCH=${1}
    SAMPLE_ID=${2}
    CMD=${3}
    TXT=${4}
    BATCH_DIR=${SAMPLES}/${BATCH}

    echo $CMD > ${BATCH_DIR}/${SAMPLE_ID}/${TXT}.txt

    if [[ -f ${BATCH_DIR}/${SAMPLE_ID}/sample.sample ]]; then
        # Delete the .sample file so it doesn't get included in training
        rm -f ${BATCH_DIR}/${SAMPLE_ID}/sample.sample
    fi
}

# Build a single sample of the pipeline with a random schedule
make_sample() {
    D=${1}
    mkdir -p ${D}
    rm -f "${D}/sample.sample"
    if [[ $D == */0 ]]; then
        # Sample 0 in each batch is best effort beam search, with no randomness
        CMD="HL_MACHINE_PARAMS=80,1,1 HL_PERMIT_FAILED_UNROLL=1 HL_SEED=${2} HL_FEATURE_FILE=${D}/sample.sample HL_WEIGHTS_DIR=${WEIGHTS_DIR} HL_RANDOM_DROPOUT=100 HL_BEAM_SIZE=20 HL_SHARED_MEMORY_LIMIT=48 ${GENERATOR} -g ${PIPELINE} -o ${D} -e static_library,h,stmt,assembly,registration target=${HL_TARGET} auto_schedule=true max_stages=${MAX_STAGES} seed=${3} -p bin/libauto_schedule.so 2> ${D}/compile_log_stderr.txt > ${D}/compile_log_stdout.txt"
    else
        # The other samples are random probes biased by the cost model
        CMD="HL_MACHINE_PARAMS=80,1,1 HL_PERMIT_FAILED_UNROLL=1 HL_SEED=${2} HL_FEATURE_FILE=${D}/sample.sample HL_WEIGHTS_DIR=${WEIGHTS_DIR} HL_RANDOM_DROPOUT=5 HL_BEAM_SIZE=1 HL_SHARED_MEMORY_LIMIT=48 ${GENERATOR} -g ${PIPELINE} -o ${D} -e static_library,h,stmt,assembly,registration target=${HL_TARGET} auto_schedule=true max_stages=${MAX_STAGES} seed=${3} -p bin/libauto_schedule.so 2> ${D}/compile_log_stderr.txt > ${D}/compile_log_stdout.txt"
    fi

    BATCH=${4}
    SAMPLE_ID=${5}

    eval $CMD
    if [[ $? != 0 ]]; then
        echo "Autoschedule failed or timed out for ${D}"
        record_failed $BATCH $SAMPLE_ID "$CMD" "autoschedule_command"
        return
    fi

    CMD="c++ -std=c++11 -I ../../include ../../tools/RunGenMain.cpp ${D}/*.registration.cpp ${D}/*.a -o ${D}/bench -ljpeg -ldl -lpthread -lz -lpng"
    eval $CMD
    if [[ $? != 0 ]]; then
        echo "Compilation failed for ${D}"
        record_failed $BATCH $SAMPLE_ID "$CMD" "compile_command"
        return
    fi
}

# Benchmark one of the random samples
benchmark_sample() {
    D=${1}
    BATCH=${4}
    SAMPLE_ID=${5}

    if [[ ! -f ${D}/bench ]]; then
        return
    fi

    CMD="HL_NUM_THREADS=32 ${D}/bench --output_extents=estimate --default_input_buffers=random:0:auto --default_input_scalars=estimate --benchmarks=all --benchmark_min_time=1 ${RUNGEN_ARGS}"

    eval $CMD > ${D}/bench.txt 2>&1
    if [[ $? != 0 ]]; then
        echo "Benchmarking failed or timed out for ${D}"
        record_failed $BATCH $SAMPLE_ID "$CMD" "benchmark_command"
        return
    fi

    # Add the runtime, pipeline id, and schedule id to the feature file
    R=$(cat ${D}/bench.txt | grep 'Benchmark for' | cut -d' ' -f8)
    CMD="./bin/augment_sample ${D}/sample.sample $R $3 $2"
    if [[ $? != 0 ]]; then
        echo "Augment sample failed for ${D}"
        record_failed $BATCH $SAMPLE_ID "$CMD" "augment_command"
    fi
}

# Don't clobber existing samples
FIRST=$(ls ${SAMPLES} | cut -d_ -f2 | sort -n | tail -n1)

for ((i=$((FIRST+1));i<1000000;i++)); do
    # Compile a batch of samples using the generator in parallel
    DIR=${SAMPLES}/batch_${i}

    # Copy the weights being used into the batch folder so that we can repro failures
    mkdir -p ${DIR}
    cp ${WEIGHTS_DIR}/* ${SAMPLES}/batch_${i}/

    for ((b=0;b<${BATCH_SIZE};b++)); do
        S=$(printf "%d%02d" $i $b)
        make_sample "${DIR}/${b}" $S $i "batch_${i}" $b &
        pids[${b}]=$!
    done

    for ((b=0;b<${BATCH_SIZE};b++)); do
        echo Compiling sample $b
        wait ${pids[${b}]}
    done

    # Kill the ones with silly predicted costs that still slipped through because randomness
    grep -r 100000000000 ${DIR} | sed 's/compile_log.*/bench/' | sort | uniq | xargs rm

    ## benchmark them serially using rungen
    for ((b=0;b<${BATCH_SIZE};b++)); do
        echo Benchmarking sample $b
        S=$(printf "%d%02d" $i $b)
        benchmark_sample "${DIR}/${b}" $S $i "batch_${i}" $b
    done

    ## retrain model weights on all samples seen so far
    if [ $DEBUG_MODE != 1 ]; then
        echo Retraining model...
        find ${SAMPLES} | grep sample$ | HL_NUM_THREADS=32 HL_WEIGHTS_DIR=${WEIGHTS_DIR} ./bin/train_cost_model 32
    else
        echo "Shared memory errors:"
        bash find_shmem_errors.sh
    fi

done
