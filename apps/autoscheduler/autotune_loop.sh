# Build the generator to autotune. This script will be autotuning the
# autoscheduler's cost model training pipeline, which is large enough
# to be interesting.
if [ $# -lt 5 -o $# -gt 6 ]; then
  echo "Usage: $0 /path/to/some.generator generatorname halide_target weights_dir autoschedule_bin_dir [generator_args_sets]"
  exit
fi

set -eu

#trap "exit" INT TERM
#trap "kill 0" EXIT

GENERATOR=${1}
PIPELINE=${2}
HL_TARGET=${3}
START_WEIGHTS_DIR=${4}
AUTOSCHED_BIN=${5}

# Read the generator-arg sets into an array. Each set is delimited
# by space; multiple values within each set are are delimited with ;
# e.g. "set1arg1=1;set1arg2=foo set2=bar set3arg1=3.14;set4arg2=42"
if [ $# -ge 6 ]; then
    IFS=' ' read -r -a GENERATOR_ARGS_SETS_ARRAY <<< "${6}"
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
HL_TARGET=x86-64-avx2
fi

if [ -z ${GENERATOR} ]; then
GENERATOR=./bin/demo.generator
fi

if [ -z ${PIPELINE} ]; then
PIPELINE=demo
fi

SAMPLES=${PWD}/samples
mkdir -p ${SAMPLES}

WEIGHTS=${SAMPLES}/weights
if [[ -d ${WEIGHTS} ]]; then
    echo Using existing weights in ${WEIGHTS}
else
    # Only copy over the weights if we don't have any already,
    # so that restarted jobs can continue from where they left off
    mkdir -p ${WEIGHTS}
    cp ${START_WEIGHTS_DIR}/*.data ${WEIGHTS}/
    echo Copying starting weights from ${START_WEIGHTS_DIR} to ${WEIGHTS}
fi

# We could add these unconditionally, but it's easier to wade thru
# results if we only add if needed
for F in disable_llvm_loop_unroll disable_llvm_loop_vectorize; do
    if [[ ! ${HL_TARGET} =~ .*${F}.* ]]; then
        HL_TARGET="${HL_TARGET}-${F}"
    fi
done

# A batch of this many samples is built in parallel, and then
# benchmarked serially.
BATCH_SIZE=32

TIMEOUT_CMD="timeout"
if [ $(uname -s) = "Darwin" ] && ! which $TIMEOUT_CMD 2>&1 >/dev/null; then
    # OSX doesn't have timeout; gtimeout is equivalent and available via Homebrew
    TIMEOUT_CMD="gtimeout"
    if ! which $TIMEOUT_CMD 2>&1 >/dev/null; then
        echo "Can't find the command 'gtimeout'. Run 'brew install coreutils' to install it."
        exit 1
    fi
fi

# Build a single sample of the pipeline with a random schedule
make_sample() {
    D=${1}
    SEED=${2}
    FNAME=${3}
    EXTRA_GENERATOR_ARGS=${4}
    mkdir -p ${D}
    rm -f "${D}/sample.sample"
    if [[ $D == */0 ]]; then
        # Sample 0 in each batch is best effort beam search, with no randomness
        dropout=100
        beam=32
    else
        # The other samples are random probes biased by the cost model
        dropout=1  # 1% chance of operating entirely greedily
        beam=1
    fi
    HL_PERMIT_FAILED_UNROLL=1 \
        HL_SEED=${SEED} \
        HL_SCHEDULE_FILE=${D}/schedule.txt \
        HL_FEATURE_FILE=${D}/sample.sample \
        HL_WEIGHTS_DIR=${WEIGHTS} \
        HL_RANDOM_DROPOUT=${dropout} \
        HL_BEAM_SIZE=${beam} \
        HL_MACHINE_PARAMS=32,24000000,40 \
        ${TIMEOUT_CMD} -k ${COMPILATION_TIMEOUT} ${COMPILATION_TIMEOUT} \
        ${GENERATOR} \
        -g ${PIPELINE} \
        -f ${FNAME} \
        -o ${D} \
        -e stmt,assembly,static_library,h,registration \
        target=${HL_TARGET} \
        auto_schedule=true \
        ${EXTRA_GENERATOR_ARGS} \
        -p ${AUTOSCHED_BIN}/libauto_schedule.so \
          2> ${D}/compile_log.txt || echo "Compilation failed or timed out for ${D}"
    

    c++ \
        -std=c++11 \
        -I ../../include \
        ../../tools/RunGenMain.cpp \
        ${D}/*.registration.cpp \
        ${D}/*.a \
        -o ${D}/bench \
        -ljpeg -ldl -lpthread -lz -lpng
}

# Benchmark one of the random samples
benchmark_sample() {
    sleep 1 # Give CPU clocks a chance to spin back up if we're thermally throttling
    D=${1}
    HL_NUM_THREADS=32 \
        ${TIMEOUT_CMD} -k ${BENCHMARKING_TIMEOUT} ${BENCHMARKING_TIMEOUT} \
        ${D}/bench \
        --output_extents=estimate \
        --default_input_buffers=random:0:estimate_then_auto \
        --default_input_scalars=estimate \
        --benchmarks=all \
            | tee ${D}/bench.txt || echo "Benchmarking failed or timed out for ${D}"

    # Add the runtime, pipeline id, and schedule id to the feature file
    R=$(cut -d' ' -f8 < ${D}/bench.txt)
    P=$3
    S=$2
    ${AUTOSCHED_BIN}/augment_sample ${D}/sample.sample $R $P $S || echo "Augment sample failed for ${D} (probably because benchmarking failed)"
}

# Don't clobber existing samples
FIRST=$(ls -d ${SAMPLES}/batch_* 2>/dev/null | sed -e "s|.*/batch_||;s|_.*||" | sort -n | tail -n1)

if [ $(uname -s) = "Darwin" ]; then
    LOCAL_CORES=`sysctl -n hw.ncpu`
else
    LOCAL_CORES=`nproc`
fi
echo Local number of cores detected as ${LOCAL_CORES}

NUM_BATCHES=1

for ((BATCH_ID=$((FIRST+1));BATCH_ID<$((FIRST+1+NUM_BATCHES));BATCH_ID++)); do

    SECONDS=0

    for ((EXTRA_ARGS_IDX=0;EXTRA_ARGS_IDX<${#GENERATOR_ARGS_SETS_ARRAY[@]};EXTRA_ARGS_IDX++)); do

        # Compile a batch of samples using the generator in parallel
        DIR=${SAMPLES}/batch_${BATCH_ID}_${EXTRA_ARGS_IDX}

        # Copy the weights being used into the batch folder so that we can repro failures
        mkdir -p ${DIR}/weights_used/
        cp ${WEIGHTS}/* ${DIR}/weights_used/

        EXTRA_GENERATOR_ARGS=${GENERATOR_ARGS_SETS_ARRAY[EXTRA_ARGS_IDX]/;/ }
        if [ ! -z "${EXTRA_GENERATOR_ARGS}" ]; then
            echo "Adding extra generator args (${EXTRA_GENERATOR_ARGS}) for batch_${BATCH_ID}"
        fi

        echo ${EXTRA_GENERATOR_ARGS} > ${DIR}/extra_generator_args.txt
    
        # Do parallel compilation in batches, so that machines with fewer than BATCH_SIZE cores
        # don't get swamped and timeout unnecessarily
        echo Compiling samples
        for ((SAMPLE_ID=0;SAMPLE_ID<${BATCH_SIZE};SAMPLE_ID++)); do
            while [[ 1 ]]; do
                RUNNING=$(jobs -r | wc -l)
                if [[ RUNNING -ge LOCAL_CORES ]]; then
                    sleep 1
                else
                    break
                fi
            done

            S=$(printf "%d%02d" $BATCH_ID $SAMPLE_ID)
            FNAME=$(printf "%s_batch_%02d_sample_%02d" ${PIPELINE} $BATCH_ID $SAMPLE_ID)
            make_sample "${DIR}/${SAMPLE_ID}" $S $FNAME "$EXTRA_GENERATOR_ARGS" &
        done
        wait

        # benchmark them serially using rungen
        for ((SAMPLE_ID=0;SAMPLE_ID<${BATCH_SIZE};SAMPLE_ID++)); do
            S=$(printf "%d%02d" $BATCH_ID $SAMPLE_ID)
            benchmark_sample "${DIR}/${SAMPLE_ID}" $S $EXTRA_ARGS_IDX
        done

        # retrain model weights on all samples seen so far
        echo Retraining model...
        
        find samples | grep sample$ | \
            HL_NUM_THREADS=32 HL_WEIGHTS_DIR=${WEIGHTS} HL_BEST_SCHEDULE_FILE=${PWD}/samples/best.txt ${AUTOSCHED_BIN}/train_cost_model ${BATCH_SIZE} 0.0001
    done

    echo Batch ${BATCH_ID} took ${SECONDS} seconds to compile, benchmark, and retrain
done
