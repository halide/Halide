# set -x

GENERATION=gen6
# Let the ftp server know we've started
CPUS=$( nproc )
HOST_ID="${CPUS}-core_${HOSTNAME}"
touch ___started.${HOST_ID}.txt
bash ./ftp_up.sh ___started.${HOST_ID}.txt ${GENERATION}
rm ___started.${HOST_ID}.txt

# Install a watchdog to kill benchmarking processes that take too long
bash ./watchdog_bench.sh &
WATCHDOG_PID=$!
function finish {
    kill $WATCHDOG_PID
}
trap finish EXIT

# Build the generator to autotune.
GENERATOR=./bin/random_pipeline.generator
PIPELINE=random_pipeline
make bin/random_pipeline.generator
make bin/RunGenMain.o

SAMPLES=${PWD}/samples

# Build some tools we need.
mkdir -p bin
make -C ../autoscheduler ../autoscheduler/bin/augment_sample
make -C ../autoscheduler ../autoscheduler/bin/train_cost_model
make -C ../autoscheduler ../autoscheduler/bin/libauto_schedule.so
cp ../autoscheduler/bin/augment_sample ../autoscheduler/bin/train_cost_model  ../autoscheduler/bin/libauto_schedule.so bin/

mkdir -p ${SAMPLES}
mkdir -p weights

cp ../autoscheduler/arm_weights/* weights/

# A batch of this many samples is built in parallel, and then
# benchmarked serially. Set to number of cores.
BATCH_SIZE=32

TARGET_PARALLELISM=${CPUS}

HL_TARGET=host-disable_llvm_loop_vectorize-disable_llvm_loop_unroll

# Build a single sample of the pipeline with a random schedule
make_sample() {
    D=${1}
    echo Building sample ${D}
    mkdir -p ${D}
    rm -f "${D}/sample.sample"
    # All samples are random probes biased by the cost model
    HL_MACHINE_PARAMS=${TARGET_PARALLELISM},1,1 HL_PERMIT_FAILED_UNROLL=1 HL_SEED=${2} HL_FEATURE_FILE=${D}/sample.sample HL_WEIGHTS_DIR=${PWD}/weights HL_RANDOM_DROPOUT=5 HL_BEAM_SIZE=1 ${GENERATOR} -g ${PIPELINE} -o ${D} -e static_library,h,stmt,assembly,registration target=${HL_TARGET} auto_schedule=true max_stages=12 seed=${3} -p ${PWD}/bin/libauto_schedule.so 2> ${D}/compile_log_stderr.txt > ${D}/compile_log_stdout.txt

    c++ -std=c++11 bin/RunGenMain.o ${D}/*.registration.cpp ${D}/*.a -o ${D}/bench -ljpeg -ldl -lpthread -lz -lpng
}

# Benchmark one of the random samples
benchmark_sample() {
    D=${1}
    HL_NUM_THREADS=${TARGET_PARALLELISM} ${D}/bench --output_extents=estimate --default_input_buffers=random:0:auto --default_input_scalars=estimate --benchmarks=all --benchmark_min_time=1 ${RUNGEN_ARGS} | tee ${D}/bench.txt

    # Add the runtime, pipeline id, and schedule id to the feature file
    R=$(cat ${D}/bench.txt | grep 'Benchmark for' | cut -d' ' -f8)
    ./bin/augment_sample ${D}/sample.sample $R $3 $2
}

# Be modest to avoid out-of-memory
echo Parallel builds: ${PARALLEL_BUILDS:=CPUS-1}

while [ 1 ]; do
    # Grab the current weights
    aws s3 sync "s3://io.halide.autoscheduler.siggraph-2019-arm/weights" weights || echo "Failed to grab weights from s3"

    ID=$((RANDOM*100 + RANDOM))

    # Compile a batch of samples using the generator in parallel
    DIR=${SAMPLES}/batch_${ID}    
    
    # Copy the weights being used into the batch folder so that we can repro failures
    mkdir -p ${DIR}
    cp weights/* ${SAMPLES}/batch_${ID}/

    echo Compiling samples
    for ((b=0;b<${BATCH_SIZE};b++)); do
        while [[ 1 ]]; do
            RUNNING=$(jobs -r | wc -l)
            if [[ RUNNING -gt PARALLEL_BUILDS ]]; then
                sleep 1
            else
                break
            fi
        done
    
        S=$(printf "%d%02d" $ID $b)
        make_sample "${DIR}/${b}" $S $ID &
	pids[${b}]=$!
    done
    echo "Finished loop, waiting"
    for pid in ${pids[*]}; do
        wait $pid
    done
    echo "...done"

    # Kill the ones with silly predicted costs that still slipped through because randomness
    grep -r 100000000000 ${DIR} | sed 's/compile_log.*/bench/' | sort | uniq | xargs rm

    # benchmark them serially using rungen
    for ((b=0;b<${BATCH_SIZE};b++)); do
        echo Benchmarking sample $b
        S=$(printf "%d%02d" $ID $b)
        benchmark_sample "${DIR}/${b}" $S $ID
    done

    # zip and upload them
    # ** UPDATE GENERATION? **
    find ${SAMPLES} -not -name bench -and -not -name random_pipeline.a | zip -@ samples_${HOST_ID}_${ID}.zip
    bash ftp_up.sh samples_${HOST_ID}_${ID}.zip ${GENERATION}
    rm samples_${HOST_ID}_${ID}.zip
    rm -rf ${SAMPLES}

done
