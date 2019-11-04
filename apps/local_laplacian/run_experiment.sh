#!/bin/bash

APP=local_laplacian
NUM_SAMPLES=$1

if [ -z $NUM_SAMPLES ]; then
NUM_SAMPLES=16
fi

# Make sure Halide is built
make -C ../../ distrib -j32

# Build the autoscheduler
make -C ../autoscheduler ../autoscheduler/bin/libauto_schedule.so -j16

# Build the app generator
make bin/host/${APP}.generator -j32

# Precompile RunGenMain
if [ ! -f bin/RunGenMain.o ]; then
    c++ -O3 -c ../../tools/RunGenMain.cpp -o bin/RunGenMain.o -I ../../distrib/include
fi

mkdir -p results

# Do all of the compilation
for ((SEED=0;SEED<${NUM_SAMPLES};SEED++)); do
    mkdir -p results/${SEED}

    echo "Running generator with seed ${SEED}"
    HL_PERMIT_FAILED_UNROLL=1 HL_SEED=${SEED} HL_RANDOM_DROPOUT=1 HL_BEAM_SIZE=1 HL_DEBUG_CODEGEN=1 \
    ./bin/host/${APP}.generator -g ${APP} -e stmt,static_library,h,assembly,registration -o results/${SEED} -p ../autoscheduler/bin/libauto_schedule.so target=host auto_schedule=true > results/${SEED}/stdout.txt 2> results/${SEED}/stderr.txt &
done
echo "Waiting for generators to finish..."
wait

for ((SEED=0;SEED<${NUM_SAMPLES};SEED++)); do
    echo "Compiling benchmarker ${SEED}"
    c++ results/${SEED}/*.{cpp,a} bin/RunGenMain.o -I ../../distrib/include/ -ljpeg -lpng -ltiff -lpthread -ldl -o results/${SEED}/benchmark &
done
echo "Waiting for compilations to finish..."
wait

# Get the benchmarks
for ((SEED=0;SEED<${NUM_SAMPLES};SEED++)); do
    echo "Running benchmark ${SEED}"
    results/${SEED}/benchmark --benchmark_min_time=1 --benchmarks=all --default_input_buffers=random:0:auto --default_input_scalars --output_extents=estimate --parsable_output > results/${SEED}/benchmark_stdout.txt 2> results/${SEED}/benchmark_stderr.txt
done
