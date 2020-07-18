#!/bin/bash

# Build the generator to autotune. This script will be autotuning the
# autoscheduler's cost model training pipeline, which is large enough
# to be interesting.
if [ $# -lt 7 -o $# -gt 8 ]; then
    echo "Usage: $0 /path/to/some.generator generatorname halide_target weights_file autoschedule_bin_dir batch_id train_only [generator_args_sets]"
    exit
fi

source $(dirname $0)/scripts/utils.sh
find_halide HALIDE_ROOT

set -eu

#trap "exit" INT TERM
#trap "kill 0" EXIT

GENERATOR=${1}
PIPELINE=${2}
HL_TARGET=${3}
START_WEIGHTS_FILE=${4}
AUTOSCHED_BIN=${5}
BATCH_ID=${6}
TRAIN_ONLY=${7}

LEARNING_RATE=${LEARNING_RATE:-0.001}

# Read the generator-arg sets into an array. Each set is delimited
# by space; multiple values within each set are are delimited with ;
# e.g. "set1arg1=1;set1arg2=foo set2=bar set3arg1=3.14;set4arg2=42"
if [ $# -ge 8 ]; then
    IFS=' ' read -r -a GENERATOR_ARGS_SETS_ARRAY <<< "${8}"
else
    declare -a GENERATOR_ARGS_SETS_ARRAY=
fi

# Ensure the length is at least 1
if [ ${#GENERATOR_ARGS_SETS_ARRAY[@]} -eq 0 ]; then
    GENERATOR_ARGS_SETS_ARRAY=( '' )
fi

COMPILATION_TIMEOUT=600s
BENCHMARKING_TIMEOUT=60s

if [ -z ${HL_TARGET} ]; then
# Use the host target -- but remove features that we don't want to train
# for by default, at least not yet (most notably, AVX512).
HL_TARGET=`${AUTOSCHED_BIN}/get_host_target avx512 avx512_knl avx512_skylake avx512_cannonlake`
fi
echo Training target is: ${HL_TARGET}

if [ -z ${GENERATOR} ]; then
GENERATOR=./bin/demo.generator
fi

if [ -z ${PIPELINE} ]; then
PIPELINE=demo
fi

SEARCH_SPACE_OPTIONS=${SEARCH_SPACE_OPTIONS:-"01111"}

SAMPLES=${SAMPLES_DIR}
mkdir -p ${SAMPLES}

WEIGHTS=${SAMPLES}/updated.weights
if [[ -f ${WEIGHTS} ]]; then
    echo Using existing weights "${WEIGHTS}"
else
    # Only copy over the weights if we don't have any already,
    # so that restarted jobs can continue from where they left off
    cp ${START_WEIGHTS_FILE} ${WEIGHTS}
    echo Copying starting weights from ${START_WEIGHTS_FILE} to ${WEIGHTS}
fi

# We could add this unconditionally, but it's easier to wade thru
# results if we only add if needed
for F in disable_llvm_loop_opt; do
    if [[ ! ${HL_TARGET} =~ .*${F}.* ]]; then
        HL_TARGET="${HL_TARGET}-${F}"
    fi
done

if [ $(uname -s) = "Darwin" ]; then
    LOCAL_CORES=`sysctl -n hw.ncpu`
else
    LOCAL_CORES=`nproc`
fi
echo Local number of cores detected as ${LOCAL_CORES}

# A batch of this many samples is built in parallel, and then
# benchmarked serially.
BATCH_SIZE=${LOCAL_CORES}
NUM_CORES=80
EPOCHS=100
NUM_GPUS=$(nvidia-smi --query-gpu=name --format=csv,noheader | wc -l)

echo "# GPUs = ${NUM_GPUS}"

# Latest git hash
GIT_HASH=$(git rev-parse --verify HEAD)

if [[ $TRAIN_ONLY != 1 ]]; then
    get_timeout_cmd TIMEOUT_CMD
else
    echo "Train only mode: ON"
    EPOCHS=10000
fi

record_command() {
    BATCH=${1}
    SAMPLE_ID=${2}
    CMD=${3}
    TXT=${4}
    FAILED=${5}
    BATCH_DIR=${SAMPLES}/${BATCH}

    echo $CMD > ${BATCH_DIR}/${SAMPLE_ID}/${TXT}.txt

    if [[ ${FAILED} == 1 && -f ${BATCH_DIR}/${SAMPLE_ID}/sample.sample ]]; then
        # Delete the .sample file so it doesn't get included in re-training
        rm -f ${BATCH_DIR}/${SAMPLE_ID}/sample.sample
    fi
}

# Build a single featurization of the pipeline with a random schedule
make_featurization() {
    D=${1}
    RANDOM_DROPOUT_SEED=${2}
    FNAME=${3}
    EXTRA_GENERATOR_ARGS=${4}
    BATCH=${5}
    SAMPLE_ID=${6}
    USED_WEIGHTS=${7}

    mkdir -p ${D}
    rm -f "${D}/${FNAME}.featurization"
    rm -f "${D}/${FNAME}.sample"
    if [[ $D == */0 ]]; then
        # Sample 0 in each batch is best effort beam search, with no randomness
        dropout=100
        beam=32
    else
        # The other samples are random probes biased by the cost model
        dropout=1  # 1% chance of operating entirely greedily
        beam=1
    fi

    local -r shared_memory_limit=48
    local -r shared_memory_sm_limit=96

    GPU=$((RANDOM % NUM_GPUS))
    CMD="CUDA_VISIBLE_DEVICES=${GPU} \
        HL_USE_MEMOIZED_FEATURES=1 \
        HL_SEARCH_SPACE_OPTIONS=${SEARCH_SPACE_OPTIONS}
        HL_SEED=${RANDOM_DROPOUT_SEED} \
        HL_WEIGHTS_DIR=${WEIGHTS} \
        HL_MEMOIZE_BLOCKS=1 \
        HL_RANDOMIZE_TILINGS=1 \
        HL_FREEZE_INLINE_COMPUTE_ROOT=1 \
        HL_RANDOM_DROPOUT=${dropout} \
        HL_BEAM_SIZE=${beam} \
        HL_SHARED_MEMORY_LIMIT=${shared_memory_limit} \
        HL_SHARED_MEMORY_SM_LIMIT=${shared_memory_sm_limit} \
        HL_MACHINE_PARAMS=${HL_MACHINE_PARAMS} \
        HL_DEBUG_AUTOSCHEDULE=1 \
        HL_DEBUG_CODEGEN=1 \
        time -f 'Compile time (s): %e' ${TIMEOUT_CMD} -k ${COMPILATION_TIMEOUT} ${COMPILATION_TIMEOUT} \
        ${GENERATOR} \
        -g ${PIPELINE} \
        -f ${FNAME} \
        -o ${D} \
        -e stmt,assembly,static_library,c_header,registration,schedule,featurization \
        target=${HL_TARGET} \
        auto_schedule=true \
        ${EXTRA_GENERATOR_ARGS} \
        -p ${AUTOSCHED_BIN}/libauto_schedule.so \
        -s Adams2019 \
        2> ${D}/compile_err.txt > ${D}/compile_log.txt"

    FAILED=0
    eval $CMD || FAILED=1

    echo "git rev-parse --verify HEAD = ${GIT_HASH}" >> ${D}/compile_err.txt

    record_command $BATCH $SAMPLE_ID "${CMD/$WEIGHTS/$USED_WEIGHTS}" "autoschedule_command" $FAILED
    if [[ $FAILED == 1 ]]; then
        echo "Autoschedule failed or timed out for ${D}" | tee -a ${D}/compile_err.txt
        return
    fi

    CMD="c++ \
        -std=c++11 \
        -I ../../include \
        ../../tools/RunGenMain.cpp \
        ${D}/*.registration.cpp \
        ${D}/*.a \
        -o ${D}/bench \
        -DHALIDE_NO_PNG -DHALIDE_NO_JPEG \
        -ldl -lpthread"

    eval $CMD
    FAILED=0
    if [[ $? != 0 ]]; then
        echo "Compile failed ${D}" | tee -a ${D}/compile_err.txt
        FAILED=1
    fi
    record_command $BATCH $SAMPLE_ID "$CMD" "compile_command" $FAILED
}

# Benchmark one of the random samples
benchmark_sample() {
    D=${1}
    BATCH=${3}
    SAMPLE_ID=${4}
    GPU_INDEX=${8}

    if [[ ! -f ${D}/bench ]]; then
        return
    fi

    CMD="CUDA_VISIBLE_DEVICES=${GPU_INDEX} HL_NUM_THREADS=${NUM_CORES} \
        ${TIMEOUT_CMD} -k ${BENCHMARKING_TIMEOUT} ${BENCHMARKING_TIMEOUT} \
        ${D}/bench"

    if [ $PIPELINE == "random_pipeline" ]; then
        CMD="${CMD} \
            --output_extents=estimate \
            --default_input_buffers=random:0:auto \
            --default_input_scalars=estimate \
            --benchmarks=all"
    else
        CMD="${CMD} \
            --estimate_all \
            --benchmarks=all"
    fi

    CMD="${CMD} 2> ${D}/bench_err.txt"

    eval $CMD | tee ${D}/bench.txt

    FAILED=0
    if [[ ! -s ${D}/bench.txt ]]; then
        echo "Benchmarking failed or timed out for ${D}"
        FAILED=1
    fi

    record_command $BATCH $SAMPLE_ID "$CMD" "benchmark_command" $FAILED

    NVPROF_TIMELINE_CMD="HL_NUM_THREADS=${NUM_CORES} \
        ${TIMEOUT_CMD} -k ${BENCHMARKING_TIMEOUT} ${BENCHMARKING_TIMEOUT} \
        nvprof --output-profile ${D}/timeline_${BATCH}_${SAMPLE_ID}.nvprof \
        ${D}/bench \
        --output_extents=estimate \
        --default_input_buffers=random:0:estimate_then_auto \
        --default_input_scalars=estimate \
        --benchmarks=all"

    NVPROF_METRICS_CMD="HL_NUM_THREADS=${NUM_CORES} \
        ${TIMEOUT_CMD} -k ${BENCHMARKING_TIMEOUT} ${BENCHMARKING_TIMEOUT} \
        nvprof --analysis-metrics -o ${D}/metrics_${BATCH}_${SAMPLE_ID}.nvprof \
        ${D}/bench \
        --output_extents=estimate \
        --default_input_buffers=random:0:estimate_then_auto \
        --default_input_scalars=estimate \
        --benchmarks=all"

    NVPROF_CMD="${NVPROF_TIMELINE_CMD} && ${NVPROF_METRICS_CMD}"
    record_command $BATCH $SAMPLE_ID "$NVPROF_CMD" "nvprof_command" $FAILED

    METRICS_CMD="HL_NUM_THREADS=${NUM_CORES} \
        ${TIMEOUT_CMD} -k ${BENCHMARKING_TIMEOUT} ${BENCHMARKING_TIMEOUT} \
        nvprof --metrics gld_transactions,gst_transactions,gld_efficiency,gst_efficiency,gld_transactions_per_request,gst_transactions_per_request,shared_load_transactions,shared_load_transactions_per_request,shared_store_transactions,shared_store_transactions_per_request,local_load_requests,local_load_transactions,local_load_transactions_per_request,local_store_requests,local_store_transactions,local_store_transactions_per_request \
        --log-file ${D}/metrics.log \
        ${D}/bench \
        --output_extents=estimate \
        --default_input_buffers=random:0:estimate_then_auto \
        --default_input_scalars=estimate \
        --benchmarks=all"

    record_command $BATCH $SAMPLE_ID "$METRICS_CMD" "metrics_command" $FAILED

    TRACE_CMD="HL_NUM_THREADS=${NUM_CORES} \
        ${TIMEOUT_CMD} -k ${BENCHMARKING_TIMEOUT} ${BENCHMARKING_TIMEOUT} \
        nvprof --print-gpu-trace \
        --log-file ${D}/trace_64.log \
        ${D}/bench \
        --output_extents=estimate \
        --default_input_buffers=random:0:estimate_then_auto \
        --default_input_scalars=estimate \
        --benchmarks=all"

    record_command $BATCH $SAMPLE_ID "$TRACE_CMD" "trace_64_command" $FAILED

    TRACE_CMD="HL_NUM_THREADS=${NUM_CORES} \
        HL_CUDA_JIT_MAX_REGISTERS=256 \
        ${TIMEOUT_CMD} -k ${BENCHMARKING_TIMEOUT} ${BENCHMARKING_TIMEOUT} \
        nvprof --print-gpu-trace \
        --log-file ${D}/trace_256.log \
        ${D}/bench \
        --output_extents=estimate \
        --default_input_buffers=random:0:estimate_then_auto \
        --default_input_scalars=estimate \
        --benchmarks=all"

    record_command $BATCH $SAMPLE_ID "$TRACE_CMD" "trace_256_command" $FAILED

    if [[ ${FAILED} == 1 ]]; then
        return
    fi

    # Add the runtime, pipeline id, and schedule id to the feature file
    R=$(cut -d' ' -f8 < ${D}/bench.txt)
    P=$5
    if [ $PIPELINE == "random_pipeline" ]; then
        P=$7
    fi
    S=$2
    FNAME=$6

    ${AUTOSCHED_BIN}/featurization_to_sample ${D}/${FNAME}.featurization $R $P $S ${D}/${FNAME}.sample || echo "featurization_to_sample failed for ${D} (probably because benchmarking failed)"

    rm ${D}/${FNAME}.a
    rm ${D}/${FNAME}.s
    rm ${D}/${FNAME}.featurization
    rm ${D}/${FNAME}.stmt
    rm ${D}/${FNAME}.h
    rm ${D}/${FNAME}.registration.cpp
}

if [[ $BATCH_ID == 0 ]]; then
    # Don't clobber existing samples
    FIRST=$(ls -d ${SAMPLES}/batch_* 2>/dev/null | sed -e "s|.*/batch_||;s|_.*||" | sort -n | tail -n1)
else
    FIRST=$((BATCH_ID-1))
fi

BATCH_ID=$((FIRST+1))
NUM_BATCHES=1
TOTAL_NUM_SAMPLES=$((NUM_BATCHES*BATCH_SIZE*${#GENERATOR_ARGS_SETS_ARRAY[@]}))

echo "Total number of samples to be generated: ${TOTAL_NUM_SAMPLES}"

benchmark_loop() {
    TOTAL_BENCHMARK_TIME=0
    sleep 1

    echo "Starting benchmark loop for samples in ${SAMPLES}/batch_${BATCH_ID}_*"
    while [[ 1 ]]; do
        local num_completed=0
        local num_active=0

        unset waitlist

        for COMPILE_ERR_FILE in $(find ${SAMPLES}/batch_${BATCH_ID}_* | grep "compile_err.txt$"); do
            SAMPLE_DIR=$(dirname "${COMPILE_ERR_FILE}")

            SAMPLE_ID=$(basename "${SAMPLE_DIR}")
            BATCH_DIR=$(dirname "${SAMPLE_DIR}")
            BATCH=$(basename "${BATCH_DIR}")
            BATCH_ID=$(echo "${BATCH}" | cut -d_ -f 2)
            EXTRA_ARGS_IDX=$(echo "${BATCH}" | cut -d_ -f 3)
            DIR=${SAMPLES}/${BATCH}

            if [ -f "${SAMPLE_DIR}/bench.txt" ] || grep -q "Autoschedule failed" ${SAMPLE_DIR}/compile_err.txt || grep -q "Compile failed" ${SAMPLE_DIR}/compile_err.txt; then
                # Either benchmarking has been completed or the sample failed to
                # compile
                num_completed=$((num_completed+1))
                continue
            fi

            if [ ! -f "${SAMPLE_DIR}/bench" ]; then
                # Sample is still compiling
                continue
            fi

            S=$(printf "%04d%04d" $BATCH_ID $SAMPLE_ID)
            FNAME=$(printf "%s_batch_%04d_sample_%04d" ${PIPELINE} $BATCH_ID $SAMPLE_ID)
            benchmark_sample "${DIR}/${SAMPLE_ID}" $S $BATCH $SAMPLE_ID $EXTRA_ARGS_IDX $FNAME $BATCH_ID $num_active &
            waitlist+=("$!")
            num_active=$((num_active+1))

            if [[ num_active -ge NUM_GPUS ]]; then
                break
            fi
        done

        CUR_SECONDS="$SECONDS"
        wait "${waitlist[@]}"
        BENCHMARK_TIME=$(("SECONDS"-CUR_SECONDS))
        TOTAL_BENCHMARK_TIME=$((TOTAL_BENCHMARK_TIME+BENCHMARK_TIME))

        if [[ num_active -eq 0 ]]; then
            # If there were no samples ready to be benchmarked, wait before
            # trying again
            sleep 1
        fi

        if [[ num_completed -eq TOTAL_NUM_SAMPLES ]]; then
            echo "Benchmarking complete."
            break
        fi

        if [[ SECONDS -ge 600 ]]; then
            echo "Benchmark queue has been active for more than 10 minutes. Exiting."
            for pid in ${waitlist[@]}; do
                kill $pid
            done
            break
        fi
    done

    echo "Benchmark time for batch: ${TOTAL_BENCHMARK_TIME}"
}

MAX_AUTOSCHEDULE_JOBS=${LOCAL_CORES}

USE_BENCHMARK_QUEUE=1
if [[ $USE_BENCHMARK_QUEUE == 1 ]] && [[ $TRAIN_ONLY != 1 ]]; then
    echo "Benchmark queue = ON"
    MAX_AUTOSCHEDULE_JOBS=$((LOCAL_CORES-NUM_GPUS))
    benchmark_loop &
    benchmark_loop_pid=("$!")
else
    echo "Benchmark queue = OFF"
fi

echo "Max. autoschedule jobs = ${MAX_AUTOSCHEDULE_JOBS}"

SECONDS=0

if [[ $TRAIN_ONLY != 1 ]]; then
    for ((EXTRA_ARGS_IDX=0;EXTRA_ARGS_IDX<${#GENERATOR_ARGS_SETS_ARRAY[@]};EXTRA_ARGS_IDX++)); do
        # Compile a batch of samples using the generator in parallel
        BATCH=batch_${BATCH_ID}_${EXTRA_ARGS_IDX}
        DIR=${SAMPLES}/${BATCH}

        # Copy the weights being used into the batch folder so that we can repro failures
        mkdir -p ${DIR}/
        cp ${WEIGHTS} ${DIR}/used.weights

        EXTRA_GENERATOR_ARGS=${GENERATOR_ARGS_SETS_ARRAY[EXTRA_ARGS_IDX]/;/ }

        if [ $PIPELINE == "random_pipeline" ]; then
            EXTRA_GENERATOR_ARGS+=" pipeline_seed=${BATCH_ID}"
        fi

        if [ ! -z "${EXTRA_GENERATOR_ARGS}" ]; then
            echo "Adding extra generator args (${EXTRA_GENERATOR_ARGS}) for batch_${BATCH_ID}"
        fi

        echo ${EXTRA_GENERATOR_ARGS} > ${DIR}/extra_generator_args.txt

        # Do parallel compilation in batches, so that machines with fewer than BATCH_SIZE cores
        # don't get swamped and timeout unnecessarily
        unset waitlist;
        first=$(printf "%04d%04d" $BATCH_ID 0)
        last=$(printf "%04d%04d" $BATCH_ID $(($BATCH_SIZE-1)))
        echo Compiling ${BATCH_SIZE} samples from ${first} to ${last}
        CUR_SECONDS="$SECONDS"
        for ((SAMPLE_ID=0;SAMPLE_ID<${BATCH_SIZE};SAMPLE_ID++)); do
            while [[ 1 ]]; do
                RUNNING=$(jobs -r | wc -l)
                if [[ RUNNING -ge MAX_AUTOSCHEDULE_JOBS ]]; then
                    sleep 1
                else
                    break
                fi
            done

            RANDOM_DROPOUT_SEED=$(printf "%04d%04d" $BATCH_ID $SAMPLE_ID)
            FNAME=$(printf "%s_batch_%04d_sample_%04d" ${PIPELINE} $BATCH_ID $SAMPLE_ID)
            make_featurization "${DIR}/${SAMPLE_ID}" $RANDOM_DROPOUT_SEED $FNAME "$EXTRA_GENERATOR_ARGS" $BATCH $SAMPLE_ID ${DIR}/used.weights &
            waitlist+=("$!")
        done

        wait "${waitlist[@]}"
        COMPILE_TIME=$(("SECONDS"-CUR_SECONDS))
        echo "Compile time for batch: ${COMPILE_TIME}"

        # benchmark them serially using rungen
        if [[ $USE_BENCHMARK_QUEUE == 0 ]]; then
            CUR_SECONDS="$SECONDS"
            for ((SAMPLE_ID=0;SAMPLE_ID<${BATCH_SIZE};SAMPLE_ID=SAMPLE_ID+NUM_GPUS)); do
                for ((INDEX=0;INDEX<NUM_GPUS;INDEX++)); do
                    SAMPLE_ID_GPU=$((SAMPLE_ID + INDEX))
                    S=$(printf "%04d%04d" $BATCH_ID $SAMPLE_ID_GPU)
                    FNAME=$(printf "%s_batch_%04d_sample_%04d" ${PIPELINE} $BATCH_ID $SAMPLE_ID_GPU)
                    benchmark_sample "${DIR}/${SAMPLE_ID_GPU}" $S $BATCH $SAMPLE_ID_GPU $EXTRA_ARGS_IDX $FNAME $BATCH_ID $INDEX &
                done
                wait
            done
            BENCHMARK_TIME=$(("SECONDS"-CUR_SECONDS))
            echo "Benchmark time for batch: ${BENCHMARK_TIME}"
        fi
    done
fi

if [[ $USE_BENCHMARK_QUEUE == 1 ]]; then
    wait "${benchmark_loop_pid}"
fi

# retrain model weights on all samples seen so far
echo Retraining model...

CUR_SECONDS="$SECONDS"
retrain_cost_model ${HALIDE_ROOT} ${SAMPLES} ${WEIGHTS} ${NUM_CORES} ${EPOCHS} ${PIPELINE} ${LEARNING_RATE}
TRAIN_TIME=$(("SECONDS"-CUR_SECONDS))
echo "Train time for batch: ${TRAIN_TIME}"

if [[ $TRAIN_ONLY == 1 ]]; then
    echo Batch ${BATCH_ID} took ${SECONDS} seconds to retrain
else
    echo Batch ${BATCH_ID} took ${SECONDS} seconds to compile, benchmark, and retrain
fi
