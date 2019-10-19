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

# A batch of this many samples is built in parallel, and then
# benchmarked serially.
BATCH_SIZE=32
NUM_CORES=80
EPOCHS=100

if [[ $TRAIN_ONLY != 1 ]]; then
  get_timeout_cmd TIMEOUT_CMD
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
    SEED=${2}
    FNAME=${3}
    EXTRA_GENERATOR_ARGS=${4}
    BATCH=${5}
    SAMPLE_ID=${6}
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

    echo "Compiling HL_SEED=${SEED} ${EXTRA_GENERATOR_ARGS}"

    CMD="HL_SEED=${SEED} \
        HL_WEIGHTS_DIR=${WEIGHTS} \
        HL_RANDOM_DROPOUT=${dropout} \
        HL_BEAM_SIZE=${beam} \
        HL_SHARED_MEMORY_LIMIT=${shared_memory_limit} \
        HL_MACHINE_PARAMS=${HL_MACHINE_PARAMS} \
        time -f 'Compile time (s): %e' ${TIMEOUT_CMD} -k ${COMPILATION_TIMEOUT} ${COMPILATION_TIMEOUT} \
        ${GENERATOR} \
        -g ${PIPELINE} \
        -f ${FNAME} \
        -o ${D} \
        -e stmt,assembly,static_library,c_header,registration,schedule,featurization \
        target=${HL_TARGET} \
        auto_schedule=true \
        ${EXTRA_GENERATOR_ARGS} \
        -p ${AUTOSCHED_BIN}/libauto_schedule.so 2> ${D}/compile_err.txt > ${D}/compile_log.txt"

    FAILED=0
    eval $CMD || FAILED=1
    record_command $BATCH $SAMPLE_ID "$CMD" "autoschedule_command" $FAILED
    if [[ $FAILED == 1 ]]; then
        echo "Autoschedule failed or timed out for ${D}"
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
        echo "Compilation failed ${D}"
        FAILED=1
    fi
    record_command $BATCH $SAMPLE_ID "$CMD" "compile_command" $FAILED
}

# Benchmark one of the random samples
benchmark_sample() {
    sleep 1 # Give CPU clocks a chance to spin back up if we're thermally throttling

    D=${1}
    BATCH=${3}
    SAMPLE_ID=${4}

    if [[ ! -f ${D}/bench ]]; then
        return
    fi

    CMD="HL_NUM_THREADS=${NUM_CORES} \
        ${TIMEOUT_CMD} -k ${BENCHMARKING_TIMEOUT} ${BENCHMARKING_TIMEOUT} \
        ${D}/bench \
        --estimate_all \
        --benchmarks=all"

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

    if [[ ${FAILED} == 1 ]]; then
        return
    fi

    # Add the runtime, pipeline id, and schedule id to the feature file
    R=$(cut -d' ' -f8 < ${D}/bench.txt)
    P=$5
    S=$2
    FNAME=$6

    ${AUTOSCHED_BIN}/featurization_to_sample ${D}/${FNAME}.featurization $R $P $S ${D}/${FNAME}.sample || echo "featurization_to_sample failed for ${D} (probably because benchmarking failed)"
}

if [[ $BATCH_ID == 0 ]]; then
  # Don't clobber existing samples
  FIRST=$(ls -d ${SAMPLES}/batch_* 2>/dev/null | sed -e "s|.*/batch_||;s|_.*||" | sort -n | tail -n1)
else
  FIRST=$((BATCH_ID-1))
fi

if [ $(uname -s) = "Darwin" ]; then
    LOCAL_CORES=`sysctl -n hw.ncpu`
else
    LOCAL_CORES=`nproc`
fi
echo Local number of cores detected as ${LOCAL_CORES}

NUM_BATCHES=1

for ((BATCH_ID=$((FIRST+1));BATCH_ID<$((FIRST+1+NUM_BATCHES));BATCH_ID++)); do
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
          if [ ! -z "${EXTRA_GENERATOR_ARGS}" ]; then
              echo "Adding extra generator args (${EXTRA_GENERATOR_ARGS}) for batch_${BATCH_ID}"
          fi

          echo ${EXTRA_GENERATOR_ARGS} > ${DIR}/extra_generator_args.txt

          # Do parallel compilation in batches, so that machines with fewer than BATCH_SIZE cores
          # don't get swamped and timeout unnecessarily
          echo -n Compiling ${BATCH_SIZE} samples
          for ((SAMPLE_ID=0;SAMPLE_ID<${BATCH_SIZE};SAMPLE_ID++)); do
              while [[ 1 ]]; do
                  RUNNING=$(jobs -r | wc -l)
                  if [[ RUNNING -ge LOCAL_CORES ]]; then
                      sleep 1
                  else
                      break
                  fi
              done

              S=$(printf "%04d%04d" $BATCH_ID $SAMPLE_ID)
              FNAME=$(printf "%s_batch_%04d_sample_%04d" ${PIPELINE} $BATCH_ID $SAMPLE_ID)
              make_featurization "${DIR}/${SAMPLE_ID}" $S $FNAME "$EXTRA_GENERATOR_ARGS" $BATCH $SAMPLE_ID &
              echo -n .
          done
          wait
          echo done.

          # benchmark them serially using rungen
          for ((SAMPLE_ID=0;SAMPLE_ID<${BATCH_SIZE};SAMPLE_ID++)); do
              S=$(printf "%04d%04d" $BATCH_ID $SAMPLE_ID)
              FNAME=$(printf "%s_batch_%04d_sample_%04d" ${PIPELINE} $BATCH_ID $SAMPLE_ID)
              benchmark_sample "${DIR}/${SAMPLE_ID}" $S $BATCH $SAMPLE_ID $EXTRA_ARGS_IDX $FNAME
          done
      done
    fi

    # retrain model weights on all samples seen so far
    echo Retraining model...

    find ${SAMPLES} -name "*.sample" | \
        ${AUTOSCHED_BIN}/retrain_cost_model \
            --epochs=${BATCH_SIZE} \
            --rates="0.0001" \
            --num_cores=${NUM_CORES} \
            --initial_weights=${WEIGHTS} \
            --weights_out=${WEIGHTS} \
            --best_benchmark=${SAMPLES}/best.${PIPELINE}.benchmark.txt \
            --best_schedule=${SAMPLES}/best.${PIPELINE}.schedule.h

    if [[ $TRAIN_ONLY == 1 ]]; then
      echo Batch ${BATCH_ID} took ${SECONDS} seconds to retrain
    else
      echo Batch ${BATCH_ID} took ${SECONDS} seconds to compile, benchmark, and retrain
    fi
done
