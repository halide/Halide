# set -x

# Let the ftp server know we've started
#IP=$( ifconfig eth0 | grep 192.168 | cut -d: -f2 | cut -d' ' -f1 )
HOSTNAME=$( hostname )
CPUS=$( grep processor /proc/cpuinfo | tail -n1 | cut -d: -f2 | bc )
CPUS=$((CPUS+1))
IP=${CPUS}-core_${hostname}
touch ___started.${IP}.txt
bash ./ftp_up.sh ___started.${IP}.txt
rm ___started.${IP}.txt

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

SAMPLES=${PWD}/samples

# Build some tools we need.
mkdir -p bin
make -C ../autoscheduler ../autoscheduler/bin/augment_sample
make -C ../autoscheduler ../autoscheduler/bin/train_cost_model
make -C ../autoscheduler ../autoscheduler/bin/libauto_schedule.so
cp ../autoscheduler/bin/augment_sample ../autoscheduler/bin/train_cost_model  ../autoscheduler/bin/libauto_schedule.so bin/

mkdir -p ${SAMPLES}
mkdir -p weights

# A batch of this many samples is built in parallel, and then
# benchmarked serially. Set to number of cores.
BATCH_SIZE=32

HL_TARGET=host-disable_llvm_loop_vectorize-disable_llvm_loop_unroll

# Build a single sample of the pipeline with a random schedule
make_sample() {
    D=${1}
    mkdir -p ${D}
    rm -f "${D}/sample.sample"
    if [[ $D == */0 ]]; then
        # Sample 0 in each batch is best effort beam search, with no randomness
        HL_MACHINE_PARAMS=32,1,1 HL_PERMIT_FAILED_UNROLL=1 HL_SEED=${2} HL_FEATURE_FILE=${D}/sample.sample HL_WEIGHTS_DIR=${PWD}/weights HL_RANDOM_DROPOUT=100 HL_BEAM_SIZE=20 ${GENERATOR} -g ${PIPELINE} -o ${D} -e static_library,h,stmt,assembly,registration target=${HL_TARGET} auto_schedule=true max_stages=12 seed=${3} -p ${PWD}/bin/libauto_schedule.so 2> ${D}/compile_log_stderr.txt > ${D}/compile_log_stdout.txt
    else
        # The other samples are random probes biased by the cost model
        HL_MACHINE_PARAMS=32,1,1 HL_PERMIT_FAILED_UNROLL=1 HL_SEED=${2} HL_FEATURE_FILE=${D}/sample.sample HL_WEIGHTS_DIR=${PWD}/weights HL_RANDOM_DROPOUT=80 HL_BEAM_SIZE=1 ${GENERATOR} -g ${PIPELINE} -o ${D} -e static_library,h,stmt,assembly,registration target=${HL_TARGET} auto_schedule=true max_stages=12 seed=${3} -p ${PWD}/bin/libauto_schedule.so 2> ${D}/compile_log_stderr.txt > ${D}/compile_log_stdout.txt
    fi

    c++ -std=c++11 -I ../../include ../../tools/RunGenMain.cpp ${D}/*.registration.cpp ${D}/*.a -o ${D}/bench -ljpeg -ldl -lpthread -lz -lpng
}

# Benchmark one of the random samples
benchmark_sample() {
    D=${1}
    HL_NUM_THREADS=32 ${D}/bench --output_extents=estimate --default_input_buffers=random:0:auto --default_input_scalars=estimate --benchmarks=all --benchmark_min_time=1 ${RUNGEN_ARGS} | tee ${D}/bench.txt

    # Add the runtime, pipeline id, and schedule id to the feature file
    R=$(cat ${D}/bench.txt | grep 'Benchmark for' | cut -d' ' -f8)
    ./bin/augment_sample ${D}/sample.sample $R $3 $2
}

while [ 1 ]; do
    ID=$((RANDOM*100 + RANDOM))

    # Compile a batch of samples using the generator in parallel
    DIR=${SAMPLES}/batch_${ID}    
    
    # Copy the weights being used into the batch folder so that we can repro failures
    mkdir -p ${DIR}
    cp weights/* ${SAMPLES}/batch_${ID}/

    for ((b=0;b<${BATCH_SIZE};b++)); do
        S=$(printf "%d%02d" $ID $b)
        make_sample "${DIR}/${b}" $S $ID
    done

    # Kill the ones with silly predicted costs that still slipped through because randomness
    grep -r 100000000000 ${DIR} | sed 's/compile_log.*/bench/' | sort | uniq | xargs rm

    # benchmark them serially using rungen
    for ((b=0;b<${BATCH_SIZE};b++)); do
        echo Benchmarking sample $b
        S=$(printf "%d%02d" $ID $b)
        benchmark_sample "${DIR}/${b}" $S $ID
    done

    # zip and upload them
    find ${SAMPLES} | zip -@ samples_${IP}_${ID}.zip
    bash ftp_up.sh samples_${IP}_${ID}.zip
    rm samples_${IP}_${ID}.zip
    rm -rf ${SAMPLES}    

done
