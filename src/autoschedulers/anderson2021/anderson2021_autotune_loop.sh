#!/bin/bash

# Autotune the given generator
if [ $# -lt 7 -o $# -gt 8 ]; then
    echo "Usage: $0 /path/to/some.generator generatorname halide_target weights_file halide_build_dir parallelism train_only [generator_args_sets]"
    exit
fi

set -eu

if [ -z ${BASH_VERSION+x} ]; then
    echo "${0} should be run as a bash script"
    exit
fi

AUTOSCHEDULER_SRC_DIR=$(dirname $0)
SCRIPTS_DIR="${AUTOSCHEDULER_SRC_DIR}/scripts"
source ${SCRIPTS_DIR}/utils.sh

GENERATOR=${1}
PIPELINE=${2}
HL_TARGET=${3}
START_WEIGHTS_FILE=${4}
HALIDE_BUILD_DIR=${5}
PARALLELISM=${6}
TRAIN_ONLY=${7}

get_halide_src_dir ${AUTOSCHEDULER_SRC_DIR} HALIDE_SRC_DIR
get_autoscheduler_build_dir ${HALIDE_BUILD_DIR} AUTOSCHEDULER_BUILD_DIR
get_tools_build_dir ${HALIDE_BUILD_DIR} TOOLS_BUILD_DIR

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
BENCHMARKING_TIMEOUT=10s

if [ -z ${CXX+x} ]; then
    echo The CXX environment variable must be set. Exiting...
    exit
fi

RUNGENMAIN="${TOOLS_BUILD_DIR}/RunGenMain.cpp.o"
if [ ! -f $RUNGENMAIN ]; then
    echo "${RUNGENMAIN} not found. Exiting..."
    exit
fi

echo Training target is: ${HL_TARGET}

if [ -z ${GENERATOR} ]; then
GENERATOR=./bin/anderson2021_demo.generator
fi

if [ -z ${PIPELINE} ]; then
PIPELINE=demo
fi

SEARCH_SPACE_OPTIONS=${SEARCH_SPACE_OPTIONS:-"1111"}

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
#for F in disable_llvm_loop_opt; do
    #if [[ ! ${HL_TARGET} =~ .*${F}.* ]]; then
        #HL_TARGET="${HL_TARGET}-${F}"
    #fi
#done

get_num_cpu_cores NUM_CPU_CORES
echo "Number of CPU cores detected as ${NUM_CPU_CORES}"

# A batch of this many samples is built in parallel, and then
# benchmarked serially.
BATCH_SIZE=80
EPOCHS=200

if ! command -v nvidia-smi > /dev/null; then
    echo "nvidia-smi is required for autotuning"
    exit
fi

NUM_GPUS=$(nvidia-smi --query-gpu=name --format=csv,noheader | wc -l)

RANDOMIZE_TILINGS="${RANDOMIZE_TILINGS:-1}"
USE_FREEZE="${USE_FREEZE:-1}"

echo "Randomize tilings = ${RANDOMIZE_TILINGS}"
echo "Use freeze = ${USE_FREEZE}"
echo "# GPUs = ${NUM_GPUS}"

USE_BENCHMARK_QUEUE="${USE_BENCHMARK_QUEUE:-1}"
BENCHMARK_QUEUE_DIR=${SAMPLES}/benchmark_queue

RETRAIN_AFTER_EACH_BATCH=${RETRAIN_AFTER_EACH_BATCH:-1}
COMPILE_ONLY=${COMPILE_ONLY:-0}

if [[ $COMPILE_ONLY == 1 ]]; then
    echo "Compile only: ON"
    RETRAIN_AFTER_EACH_BATCH=0
    USE_BENCHMARK_QUEUE=0
else
    echo "Compile only: OFF"
fi

ENABLE_BEAM_SEARCH=${ENABLE_BEAM_SEARCH:-1}
if [[ ${ENABLE_BEAM_SEARCH} == 1 ]]; then
    echo "Beam search: ON"
else
    echo "Beam search: OFF"
fi

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

    if [[ $D == */0 && ${ENABLE_BEAM_SEARCH} == 1 ]]; then
        # Sample 0 in each batch is best effort beam search, with no randomness
        dropout=100
        beam=32
    else
        # The other samples are random probes biased by the cost model
        dropout=1  # 1% chance of operating entirely greedily
        beam=1
    fi

    # TODO: make these arguments to this file
    local -r shared_memory_limit=48
    local -r shared_memory_sm_limit=96
    local -r active_block_limit=32
    local -r active_warp_limit=64

    GPU=$((RANDOM % NUM_GPUS))
    CMD="HL_DEBUG_AUTOSCHEDULE=1 \
        /bin/time -f 'Compile time (s): %e' ${TIMEOUT_CMD} -k ${COMPILATION_TIMEOUT} ${COMPILATION_TIMEOUT} \
        ${GENERATOR} \
        -g ${PIPELINE} \
        -f ${FNAME} \
        -o ${D} \
        -e stmt,assembly,static_library,c_header,registration,schedule,featurization \
        target=${HL_TARGET} \
        ${EXTRA_GENERATOR_ARGS} \
        -p ${AUTOSCHEDULER_BUILD_DIR}/libautoschedule_anderson2021.so \
        autoscheduler=Anderson2021 \
        autoscheduler.parallelism=${PARALLELISM} \
        autoscheduler.beam_size=${beam} \
        autoscheduler.random_dropout=${dropout} \
        autoscheduler.random_dropout_seed=${RANDOM_DROPOUT_SEED} \
        autoscheduler.weights_path=${WEIGHTS} \
        autoscheduler.randomize_tilings=${RANDOMIZE_TILINGS} \
        autoscheduler.search_space_options=${SEARCH_SPACE_OPTIONS} \
        autoscheduler.freeze_inline_compute_root=${USE_FREEZE} \
        autoscheduler.shared_memory_limit_kb=${shared_memory_limit} \
        autoscheduler.shared_memory_sm_limit_kb=${shared_memory_sm_limit} \
        autoscheduler.active_block_limit=${active_block_limit} \
        autoscheduler.active_warp_limit=${active_warp_limit} \
        2> ${D}/compile_err.txt > ${D}/compile_log.txt"

    FAILED=0
    eval $CMD || FAILED=1

    echo "git rev-parse --verify HEAD = ${GIT_HASH}" >> ${D}/compile_err.txt

    record_command $BATCH $SAMPLE_ID "${CMD/$WEIGHTS/$USED_WEIGHTS}" "autoschedule_command" $FAILED
    if [[ $FAILED == 1 ]]; then
        echo "Autoschedule failed or timed out for ${D}" | tee -a ${D}/compile_err.txt
        if [[ $USE_BENCHMARK_QUEUE == 1 ]]; then
            touch "${BENCHMARK_QUEUE_DIR}/${BATCH}-${SAMPLE_ID}-failed"
        fi
        return
    fi

    LIBPNG_CFLAGS=$(libpng-config --cflags)
    LIBPNG_LIBS=$(libpng-config --ldflags)
    CMD="${CXX} \
        -std=c++11 \
        -O3
        -I ../../include \
        ${LIBPNG_CFLAGS} \
        ${RUNGENMAIN} \
        ${D}/*.registration.cpp \
        ${D}/*.a \
        -o ${D}/bench \
        -ljpeg ${LIBPNG_LIBS} -ldl -lpthread"

    eval $CMD
    FAILED=0
    if [[ $? != 0 ]]; then
        echo "Compile failed ${D}" | tee -a ${D}/compile_err.txt
        FAILED=1
        if [[ $USE_BENCHMARK_QUEUE == 1 ]]; then
            touch "${BENCHMARK_QUEUE_DIR}/${BATCH}-${SAMPLE_ID}-failed"
        fi
    else
        if [[ $USE_BENCHMARK_QUEUE == 1 ]]; then
            touch "${BENCHMARK_QUEUE_DIR}/${BATCH}-${SAMPLE_ID}"
        fi
    fi

    rm ${D}/${FNAME}.a
    rm ${D}/${FNAME}.s
    rm ${D}/${FNAME}.h
    rm ${D}/${FNAME}.registration.cpp
    rm ${D}/compile_log.txt
}

IMAGES_DIR="${HALIDE_SRC_DIR}/apps/images"

# Benchmark one of the random samples
benchmark_sample() {
    D=${1}
    BATCH=${3}
    SAMPLE_ID=${4}
    GPU_INDEX=${8}

    if [[ ! -f ${D}/bench ]]; then
        if [[ $USE_BENCHMARK_QUEUE == 1 ]]; then
            mv "${BENCHMARK_QUEUE_DIR}/${BATCH}-${SAMPLE_ID}-benchmarking-gpu_${GPU_INDEX}" "${BENCHMARK_QUEUE_DIR}/${BATCH}-${SAMPLE_ID}-completed"
        fi
        return
    fi

    CMD="CUDA_VISIBLE_DEVICES=${GPU_INDEX} HL_NUM_THREADS=${PARALLELISM} \
        ${TIMEOUT_CMD} -k ${BENCHMARKING_TIMEOUT} ${BENCHMARKING_TIMEOUT} \
        ${D}/bench"

    get_bench_args ${IMAGES_DIR} ${PIPELINE} ${D} BENCH_ARGS
    CMD="${CMD} \
        ${BENCH_ARGS} \
        --benchmarks=all"

    CMD="${CMD} 2> ${D}/bench_err.txt"

    eval $CMD | tee ${D}/bench.txt

    FAILED=0
    if [[ ! -s ${D}/bench.txt ]]; then
        echo "Benchmarking failed or timed out for ${D}"
        FAILED=1
    fi

    record_command $BATCH $SAMPLE_ID "$CMD" "benchmark_command" $FAILED

    if [[ ${FAILED} == 1 ]]; then
        if [[ $USE_BENCHMARK_QUEUE == 1 ]]; then
            mv "${BENCHMARK_QUEUE_DIR}/${BATCH}-${SAMPLE_ID}-benchmarking-gpu_${GPU_INDEX}" "${BENCHMARK_QUEUE_DIR}/${BATCH}-${SAMPLE_ID}-completed"
        fi
        return
    fi

    # Add the runtime, pipeline id, and schedule id to the feature file
    R=$(cut -d' ' -f8 < ${D}/bench.txt)
    P=$5
    S=$2
    FNAME=$6

    ${AUTOSCHEDULER_BUILD_DIR}/featurization_to_sample ${D}/${FNAME}.featurization $R $P $S ${D}/${FNAME}.sample || echo "featurization_to_sample failed for ${D} (probably because benchmarking failed)"

    rm ${D}/${FNAME}.featurization
    rm ${D}/bench
    rm ${D}/${FNAME}.stmt

    if [[ $USE_BENCHMARK_QUEUE == 1 ]]; then
        mv "${BENCHMARK_QUEUE_DIR}/${BATCH}-${SAMPLE_ID}-benchmarking-gpu_${GPU_INDEX}" "${BENCHMARK_QUEUE_DIR}/${BATCH}-${SAMPLE_ID}-completed"
    fi
}

NUM_BATCHES=${NUM_BATCHES:-1}
TOTAL_NUM_SAMPLES=$((NUM_BATCHES*BATCH_SIZE*${#GENERATOR_ARGS_SETS_ARRAY[@]}))

echo "Num batches: ${NUM_BATCHES}"
echo "Total number of samples to be generated: ${TOTAL_NUM_SAMPLES}"

if [[ ${RETRAIN_AFTER_EACH_BATCH} == 1 ]]; then
    NUM_SAMPLES_PER_QUEUE=$((BATCH_SIZE*${#GENERATOR_ARGS_SETS_ARRAY[@]}))
else
    NUM_SAMPLES_PER_QUEUE=$((NUM_BATCHES*BATCH_SIZE*${#GENERATOR_ARGS_SETS_ARRAY[@]}))
fi

MAX_BENCHMARK_TIME=$((NUM_SAMPLES_PER_QUEUE*660))

echo "Number of samples per queue: ${NUM_SAMPLES_PER_QUEUE}"
echo "Max. benchmark time: ${MAX_BENCHMARK_TIME}"

echo "Retrain after each batch: ${RETRAIN_AFTER_EACH_BATCH}"

benchmark_loop() {
    mkdir -p ${BENCHMARK_QUEUE_DIR}

    START_TIME="$SECONDS"
    MAX_TIME=${MAX_BENCHMARK_TIME}
    sleep 1

    echo "Starting benchmark loop for samples in ${SAMPLES}/*"
    echo "Max. benchmark loop time = ${MAX_TIME} seconds"

    local num_completed=0
    while [[ 1 ]]; do
        unset waitlist

        for FILE in $(ls ${BENCHMARK_QUEUE_DIR}); do
            if [[ $FILE == *"failed" ]]; then
                # The sample failed to compile
                num_completed=$((num_completed+1))
                rm "${BENCHMARK_QUEUE_DIR}/${FILE}"
                continue
            fi

            SAMPLE_ID=$(echo "${FILE}" | cut -d- -f 2)
            BATCH=$(echo "${FILE}" | cut -d- -f 1)
            SAMPLE_DIR="${SAMPLES}/${BATCH}/${SAMPLE_ID}"

            # We sometimes encounter spurious permission denied errors. Usually,
            # retrying will resolve them so remove from this file the
            # '-completed' tag and let it be benchmarked again
            if [[ -f "${SAMPLE_DIR}/bench_err.txt" ]]; then
                if grep -q "Permission denied" "${SAMPLE_DIR}/bench_err.txt"; then
                    FILE=${FILE%-completed}
                fi
            fi

            if [[ -f "${SAMPLE_DIR}/bench.txt" ]] && [[ $FILE == *"-completed" ]]; then
                # Benchmarking has been completed
                num_completed=$((num_completed+1))
                rm "${BENCHMARK_QUEUE_DIR}/${FILE}"
                continue
            fi

            if [[ $FILE == *"benchmarking"* ]]; then
                # Sample is still benchmarking
                continue
            fi

            BATCH_ID=$(echo "${BATCH}" | cut -d_ -f 2)
            EXTRA_ARGS_IDX=$(echo "${BATCH}" | cut -d_ -f 3)
            DIR=${SAMPLES}/${BATCH}

            while [[ 1 ]]; do
                if find_unused_gpu ${BENCHMARK_QUEUE_DIR} ${NUM_GPUS} gpu_id; then
                    S=$(printf "%04d%04d" $BATCH_ID $SAMPLE_ID)
                    FNAME=$(printf "%s_batch_%04d_sample_%04d" ${PIPELINE} $BATCH_ID $SAMPLE_ID)
                    # Mark this file with gpu_${gpu_id} so we know that GPU is
                    # occupied
                    mv "${BENCHMARK_QUEUE_DIR}/${FILE}" "${BENCHMARK_QUEUE_DIR}/${FILE}-benchmarking-gpu_${gpu_id}"
                    benchmark_sample "${DIR}/${SAMPLE_ID}" $S $BATCH $SAMPLE_ID $EXTRA_ARGS_IDX $FNAME $BATCH_ID $gpu_id &
                    waitlist+=("$!")
                    break
                else
                    # All GPUs are in use
                    sleep 0.1
                fi
            done
        done

        if [[ num_completed -eq NUM_SAMPLES_PER_QUEUE ]]; then
            wait "${waitlist[@]}"
            echo "Benchmarking complete."
            break
        fi

        ELAPSED_TIME=$(("SECONDS"-START_TIME))
        if [[ ELAPSED_TIME -ge MAX_TIME ]]; then
            echo "Benchmark queue has been active for more than ${MAX_TIME} seconds. Exiting."
            for pid in ${waitlist[@]}; do
                kill $pid
            done
            break
        fi
    done

    TOTAL_BENCHMARK_TIME=$(("SECONDS"-START_TIME))
    echo "Benchmark time for batch: ${TOTAL_BENCHMARK_TIME}"
    rm -rf ${BENCHMARK_QUEUE_DIR}
}

MAX_AUTOSCHEDULE_JOBS=${NUM_CPU_CORES}

BENCHMARK_QUEUE_ENABLED=0

if [[ $USE_BENCHMARK_QUEUE == 1 ]] && [[ $TRAIN_ONLY != 1 ]]; then
    # Include 1 job for the benchmark loop
    MAX_AUTOSCHEDULE_JOBS=$((NUM_CPU_CORES-NUM_GPUS-1))
    if [[ MAX_AUTOSCHEDULE_JOBS -le 0 ]]; then
        MAX_AUTOSCHEDULE_JOBS=${NUM_CPU_CORES}
        echo "Not enough cores available to use the benchmark queue"
        echo "Benchmark queue = OFF"
    else
        BENCHMARK_QUEUE_ENABLED=1
        echo "Benchmark queue = ON"
    fi
else
    echo "Benchmark queue = OFF"
fi

echo "Max. concurrent autoschedule jobs = ${MAX_AUTOSCHEDULE_JOBS}"

SECONDS=0

if [[ $TRAIN_ONLY != 1 ]]; then
    if [[ $BENCHMARK_QUEUE_ENABLED == 1 && $RETRAIN_AFTER_EACH_BATCH == 0 ]]; then
        echo "Starting benchmark queue"
        benchmark_loop &
        benchmark_loop_pid=("$!")
        echo "Starting PID: ${benchmark_loop_pid}"
    fi

    for ((BATCH_IDX=0;BATCH_IDX<${NUM_BATCHES};BATCH_IDX++)); do
        if [[ $BENCHMARK_QUEUE_ENABLED == 1 && $RETRAIN_AFTER_EACH_BATCH == 1 ]]; then
            echo "Starting benchmark queue"
            benchmark_loop &
            benchmark_loop_pid=("$!")
            echo "Starting PID: ${benchmark_loop_pid}"
        fi

        while [[ 1 ]]; do
            BATCH_ID=$(od -vAn -N3 -tu4 < /dev/urandom | awk '{print $1}')

            if [ ! -d "${SAMPLES}/batch_${BATCH_ID}_0" ]; then
                break
            fi
        done

        echo "Starting compiling of new batch with id: ${BATCH_ID}"

        for ((EXTRA_ARGS_IDX=0;EXTRA_ARGS_IDX<${#GENERATOR_ARGS_SETS_ARRAY[@]};EXTRA_ARGS_IDX++)); do
            # Compile a batch of samples using the generator in parallel
            BATCH=batch_${BATCH_ID}_${EXTRA_ARGS_IDX}_${RANDOMIZE_TILINGS}_${USE_FREEZE}
            DIR=${SAMPLES}/${BATCH}

            # Copy the weights being used into the batch folder so that we can repro failures
            mkdir -p ${DIR}/
            cp ${WEIGHTS} ${DIR}/used.weights

            EXTRA_GENERATOR_ARGS=${GENERATOR_ARGS_SETS_ARRAY[EXTRA_ARGS_IDX]/;/ }

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

            # benchmark them serially using rungen
            if [[ $USE_BENCHMARK_QUEUE == 0 && ${COMPILE_ONLY} == 0 ]]; then
                wait "${waitlist[@]}"
                COMPILE_TIME=$((SECONDS-CUR_SECONDS))
                echo "Compile time for batch: ${COMPILE_TIME}"

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
                BENCHMARK_TIME=$((SECONDS-CUR_SECONDS))
                echo "Benchmark time for batch: ${BENCHMARK_TIME}"
            fi

            if [[ ${COMPILE_ONLY} == 1 ]]; then
                wait "${waitlist[@]}"
            fi
        done

        if [[ ${RETRAIN_AFTER_EACH_BATCH} == 1 ]]; then
            if [[ $BENCHMARK_QUEUE_ENABLED == 1 ]]; then
                wait "${waitlist[@]}"
                echo "Waiting for benchmarking to complete"
                echo "Waiting PID: ${benchmark_loop_pid}"
                wait "${benchmark_loop_pid}"
            fi

            CUR_SECONDS="$SECONDS"
            retrain_cost_model ${HALIDE_BUILD_DIR} ${SAMPLES} ${WEIGHTS} ${PARALLELISM} ${EPOCHS} ${PIPELINE} ${LEARNING_RATE}
            TRAIN_TIME=$((SECONDS-CUR_SECONDS))
            echo "Train time for batch with ID = ${BATCH_ID}: ${TRAIN_TIME}"
        fi
        BATCH_ID=$((BATCH_ID+1))
    done

    if [[ ${BENCHMARK_QUEUE_ENABLED} == 1 && ${RETRAIN_AFTER_EACH_BATCH} == 0 ]]; then
        wait "${waitlist[@]}"
        echo "Waiting for benchmarking to complete"
        echo "Waiting PID: ${benchmark_loop_pid}"
        wait "${benchmark_loop_pid}"
    fi
fi

if [[ ${RETRAIN_AFTER_EACH_BATCH} == 1 || ${COMPILE_ONLY} == 1 ]]; then
    exit
fi

# retrain model weights on all samples seen so far
echo Retraining model...

CUR_SECONDS="$SECONDS"
retrain_cost_model ${HALIDE_SRC_DIR} ${SAMPLES} ${WEIGHTS} ${PARALLELISM} ${EPOCHS} ${PIPELINE} ${LEARNING_RATE}
TRAIN_TIME=$((SECONDS-CUR_SECONDS))
echo "Num batches = ${NUM_BATCHES}. Train time: ${TRAIN_TIME}"

if [[ $TRAIN_ONLY == 1 ]]; then
    echo Num batches = ${NUM_BATCHES}. Took ${SECONDS} seconds to retrain
else
    echo Num batches = ${NUM_BATCHES}. Took ${SECONDS} seconds to compile, benchmark, and retrain
fi
