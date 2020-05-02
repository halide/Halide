#!/bin/bash

APP=$(basename ${PWD})
FIRST_SEED=$1
LAST_SEED=$2

echo Running experiment for app $APP

if [ -z $FIRST_SEED ]; then
FIRST_SEED=0
fi

if [ -z $LAST_SEED ]; then
LAST_SEED=16
fi

# Make sure Halide is built
make -C ../../ distrib -j32

# Build the autoscheduler
HL_USE_SYNTHESIZED_RULES=0 make -C ../autoscheduler bin/libauto_schedule.so -j16

# Build the app generator
make bin/host/${APP}.generator -j32

# Make a runtime
./bin/host/${APP}.generator -r runtime -o bin/host target=host

# Precompile RunGenMain
if [ ! -f bin/RunGenMain.o ]; then
    c++ -std=c++11 -O3 -c ../../tools/RunGenMain.cpp -o bin/RunGenMain.o -I ../../distrib/include -I /opt/local/include
fi

mkdir -p results
mkdir -p results_baseline

wait_for_idle () {
    while [ 1 ]; do
        NUM_GENERATORS_RUNNING=$(ps | grep [.]generator | wc -l)
        if [ $NUM_GENERATORS_RUNNING -lt 24 ]; then
            break
        fi
        sleep 1
    done
}

# Do all of the compilation
for ((SEED=${FIRST_SEED};SEED<${LAST_SEED};SEED++)); do
    mkdir -p results/${SEED}
    mkdir -p results_baseline/${SEED}    

    # Every 8, wait for until more cores become idle
    if [[ $(expr $SEED % 8) == 7 ]]; then
        wait_for_idle
    fi
    
    echo "Running generator with seed ${SEED}"    
    HL_USE_SYNTHESIZED_RULES=1 \
    HL_PERMIT_FAILED_UNROLL=1 \
    HL_SEED=${SEED} \
    HL_RANDOM_DROPOUT=1 \
    HL_BEAM_SIZE=1 \
    HL_DEBUG_CODEGEN=1 \
    ./bin/host/${APP}.generator -g ${APP} -e stmt,static_library,h,assembly,registration,compiler_log -o results/${SEED} -p ../autoscheduler/bin/libauto_schedule.so target=host-no_runtime auto_schedule=true -s Adams2019  > results/${SEED}/stdout.txt 2> results/${SEED}/stderr.txt  &

    HL_USE_SYNTHESIZED_RULES=0 \
    HL_PERMIT_FAILED_UNROLL=1 \
    HL_SEED=${SEED} \
    HL_RANDOM_DROPOUT=1 \
    HL_BEAM_SIZE=1 \
    HL_DEBUG_CODEGEN=1 \
    ./bin/host/${APP}.generator -g ${APP} -e stmt,static_library,h,assembly,registration,compiler_log -o results_baseline/${SEED} -p ../autoscheduler/bin/libauto_schedule.so target=host-no_runtime auto_schedule=true -s Adams2019 > results_baseline/${SEED}/stdout.txt 2> results_baseline/${SEED}/stderr.txt    &
done
echo "Waiting for generators to finish..."
wait

for ((SEED=${FIRST_SEED};SEED<${LAST_SEED};SEED++)); do
    # Every 8, wait for until more cores become idle
    if [[ $(expr $SEED % 8) == 7 ]]; then
        wait_for_idle
    fi

    echo "Compiling benchmarker ${SEED}"
    for r in results results_baseline; do
        c++ -std=c++11 ${r}/${SEED}/*.{cpp,a} bin/RunGenMain.o bin/host/runtime.a -I ../../distrib/include/ -L/opt/local/lib -ljpeg -lpng -ltiff -lpthread -ldl -o ${r}/${SEED}/benchmark &
    done
    
done
echo "Waiting for compilations to finish..."
wait

# Get the benchmarks
for ((SEED=${FIRST_SEED};SEED<${LAST_SEED};SEED++)); do
    echo "Running benchmark ${SEED}"
    for r in results results_baseline; do 
        # Let things cool down before benchmarking
        sleep 2
        # Most of the autoschedules do better at 16 threads on my machine. Probably due to avx-512 use
        HL_NUM_THREADS=16 ${r}/${SEED}/benchmark --benchmark_min_time=1 --benchmarks=all --default_input_buffers=random:0:auto --default_input_scalars --output_extents=estimate --parsable_output > ${r}/${SEED}/benchmark_stdout.txt 2> ${r}/${SEED}/benchmark_stderr.txt
        ${r}/${SEED}/benchmark --benchmark_min_time=0 --track_memory --benchmarks=all --default_input_buffers=random:0:auto --default_input_scalars --output_extents=estimate --parsable_output > ${r}/${SEED}/memory_stdout.txt 2> ${r}/${SEED}/memory_stderr.txt
    done
done

echo "Aggregating results..."

for r in results results_baseline; do 
    for ((SEED=${FIRST_SEED};SEED<${LAST_SEED};SEED++)); do    
        grep BEST_TIME_MSEC ${r}/${SEED}/benchmark_stdout.txt | cut -d' ' -f5
    done | paste -s -d+ /dev/stdin | bc > ${r}/total_runtime.txt

    for ((SEED=${FIRST_SEED};SEED<${LAST_SEED};SEED++)); do    
        grep memory ${r}/${SEED}/memory_stdout.txt | cut -d' ' -f4
    done | paste -s -d+ /dev/stdin | bc > ${r}/total_memory.txt

    for ((SEED=${FIRST_SEED};SEED<${LAST_SEED};SEED++)); do    
        grep Lower.cpp ${r}/${SEED}/stderr.txt | cut -d' ' -f5
    done | paste -s -d+ /dev/stdin | bc > ${r}/total_halide_compile_time.txt

    for ((SEED=${FIRST_SEED};SEED<${LAST_SEED};SEED++)); do    
        grep CodeGen_LLVM.cpp ${r}/${SEED}/stderr.txt | cut -d' ' -f5
    done | paste -s -d+ /dev/stdin | bc > ${r}/total_llvm_optimization_time.txt

    for ((SEED=${FIRST_SEED};SEED<${LAST_SEED};SEED++)); do    
        # Don't count emission time for both assembly and the static library
        grep LLVM_Output.cpp ${r}/${SEED}/stderr.txt | head -n1 | cut -d' ' -f5
    done | paste -s -d+ /dev/stdin | bc > ${r}/total_llvm_backend_time.txt                
done
