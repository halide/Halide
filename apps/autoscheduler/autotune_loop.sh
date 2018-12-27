# Build the generator to autotune. This script will be autotuning the
# autoscheduler's cost model training pipeline, which is large enough
# to be interesting.
if [ $# -ne 3 ]; then
  echo "Usage: $0 /path/to/some.generator generatorname halide_target"
  exit
fi

set -eu

GENERATOR=${1}
PIPELINE=${2}
HL_TARGET=${3}

COMPILATION_TIMEOUT=120s
BENCHMARKING_TIMEOUT=60s

if [ -z ${HL_TARGET} ]; then
HL_TARGET=x86-64-avx2-disable_llvm_loop_unroll-disable_loop_loop_vectorize
fi

if [ -z ${GENERATOR} ]; then
GENERATOR=./bin/demo.generator
fi

if [ -z ${PIPELINE} ]; then
PIPELINE=demo
fi

SAMPLES=${PWD}/samples

mkdir -p ${SAMPLES}

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
    mkdir -p ${D}
    rm -f "${D}/sample.sample"
    if [[ $D == */0 ]]; then
        # Sample 0 in each batch is best effort beam search, with no randomness
        dropout=100
        beam=50
    else
        # The other samples are random probes biased by the cost model
        dropout=25
        beam=1
    fi
    HL_PERMIT_FAILED_UNROLL=1 \
        HL_MACHINE_PARAMS=32,1,1 \
        HL_SEED=${SEED} \
        HL_SCHEDULE_FILE=${D}/schedule.txt \
        HL_FEATURE_FILE=${D}/sample.sample \
        HL_WEIGHTS_DIR=${PWD}/weights \
        HL_RANDOM_DROPOUT=${dropout} \
        HL_BEAM_SIZE=${beam} \
        HL_MACHINE_PARAMS=32,1,1 \
        ${TIMEOUT_CMD} -k ${COMPILATION_TIMEOUT} ${COMPILATION_TIMEOUT} \
        ${GENERATOR} \
        -g ${PIPELINE} \
        -f ${FNAME} \
        -o ${D} \
        -e stmt,assembly,static_library,h,registration \
        target=${HL_TARGET} \
        auto_schedule=true \
        -p bin/libauto_schedule.so \
            2> ${D}/compile_log.txt || echo "Compilation failed or timed out"

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
        --benchmark_min_time=1 \
            | tee ${D}/bench.txt || echo "Benchmarking failed or timed out"

    # Add the runtime, pipeline id, and schedule id to the feature file
    R=$(cut -d' ' -f8 < ${D}/bench.txt)
    P=0
    S=$2
    ./bin/augment_sample ${D}/sample.sample $R $P $S || echo "Augment sample failed (probably because benchmarking failed)"
}

# Don't clobber existing samples
FIRST=$(ls ${SAMPLES} | cut -d_ -f2 | sort -n | tail -n1)

for ((i=$((FIRST+1));i<1000000;i++)); do
    # Compile a batch of samples using the generator in parallel
    DIR=${SAMPLES}/batch_${i}

    # Copy the weights being used into the batch folder so that we can repro failures
    mkdir -p ${DIR}
    cp weights/* ${SAMPLES}/batch_${i}/

    echo Compiling ${BATCH_SIZE} samples for batch_${i}...
    for ((b=0;b<${BATCH_SIZE};b++)); do
        S=$(printf "%d%02d" $i $b)
        FNAME=$(printf "%s_batch_%02d_sample_%02d" ${PIPELINE} $i $b)
        make_sample "${DIR}/${b}" $S $FNAME &
    done
    wait

    # benchmark them serially using rungen
    for ((b=0;b<${BATCH_SIZE};b++)); do
        S=$(printf "%d%02d" $i $b)
        benchmark_sample "${DIR}/${b}" $S
    done

    # retrain model weights on all samples seen so far
    echo Retraining model...
    find ${SAMPLES} | grep sample$ | \
        HL_NUM_THREADS=32 HL_WEIGHTS_DIR=weights HL_BEST_SCHEDULE_FILE=${PWD}/samples/best.txt ./bin/train_cost_model 1000

done
