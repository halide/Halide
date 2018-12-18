# Build the generator to autotune. This script will be autotuning the
# autoscheduler's cost model training pipeline, which is large enough
# to be interesting.
GENERATOR=./bin/demo.generator
PIPELINE=demo

HL_TARGET=x86-64-avx2

SAMPLES=samples

mkdir -p ${SAMPLES}

# A batch of this many samples is built in parallel, and then
# benchmarked serially. 
BATCH_SIZE=32

# Build a single sample of the pipeline with a random schedule
make_sample() {
    D=${1}
    mkdir -p ${D}
    rm -f "${D}/sample.sample"
    if [[ $D == */0 ]]; then
        # Sample 0 in each batch is best effort beam search, with no randomness
        HL_PERMIT_FAILED_UNROLL=1 HL_MACHINE_PARAMS=32,1,1 HL_SEED=${2} HL_FEATURE_FILE=${D}/sample.sample HL_WEIGHTS_DIR=${PWD}/weights HL_RANDOM_DROPOUT=100 HL_BEAM_SIZE=50 ${GENERATOR} -g ${PIPELINE} -o ${D} target=${HL_TARGET} auto_schedule=true -p bin/auto_schedule.so 2> ${D}/compile_log.txt
    else
        # The other samples are random probes biased by the cost model
        HL_PERMIT_FAILED_UNROLL=1 HL_MACHINE_PARAMS=32,1,1 HL_SEED=${2} HL_FEATURE_FILE=${D}/sample.sample HL_WEIGHTS_DIR=${PWD}/weights HL_RANDOM_DROPOUT=50 HL_BEAM_SIZE=1 ${GENERATOR} -g ${PIPELINE} -o ${D} target=${HL_TARGET} auto_schedule=true -p bin/auto_schedule.so 2> ${D}/compile_log.txt
    fi
    
    c++ -std=c++11 -DHL_RUNGEN_FILTER_HEADER="\"${D}/${PIPELINE}.h\"" -I ../../include ../../tools/RunGenMain.cpp ../../tools/RunGenStubs.cpp  ${D}/*.a -o ${D}/bench -ljpeg -ldl -lpthread -lz -lpng    
}

# Benchmark one of the random samples
benchmark_sample() {
    D=${1}
    HL_NUM_THREADS=32 ${D}/bench --output_extents=estimate --default_input_buffers=random:0:estimate_then_auto --default_input_scalars=estimate --benchmarks=all --benchmark_min_time=1 | tee ${D}/bench.txt

    # Add the runtime, pipeline id, and schedule id to the feature file
    R=$(cut -d' ' -f8 < ${D}/bench.txt)
    P=0
    S=$2
    ./bin/augment_sample ${D}/sample.sample $R $P $S
}

# Don't clobber existing samples
FIRST=$(ls ${SAMPLES} | cut -d_ -f2 | sort -n | tail -n1)

for ((i=$((FIRST+1));i<1000000;i++)); do
    # Compile a batch of samples using the generator in parallel
    DIR=${SAMPLES}/batch_${i}

    for ((b=0;b<${BATCH_SIZE};b++)); do
        echo Compiling sample $b
        S=$(printf "%d%02d" $i $b)
        make_sample "${DIR}/${b}" $S 1 &
    done
    wait
    
    # benchmark them serially using rungen
    for ((b=0;b<${BATCH_SIZE};b++)); do
        echo Benchmarking sample $b
        S=$(printf "%d%02d" $i $b)
        benchmark_sample "${DIR}/${b}" $S
    done
    
    # retrain model weights on all samples seen so far
    echo Retraining model...
    find ${SAMPLES} | grep sample$ | HL_NUM_THREADS=32 HL_WEIGHTS_DIR=weights ./bin/train_cost_model 1000
    
done
