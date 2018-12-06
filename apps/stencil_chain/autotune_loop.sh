
make_sample() {
    D=${1}
    mkdir -p ${D}
    HL_SEED=${2} HL_FEATURE_FILE=${D}/sample.sample HL_WEIGHTS_DIR=${PWD}/weights HL_RANDOM_DROPOUT=90 HL_BEAM_SIZE=1 ./bin/stencil_chain.generator -g stencil_chain -o ${D} target=host-new_autoscheduler stencils=12 auto_schedule=true 2> ${D}/compile_log.txt
    
    c++ -std=c++11 -DHL_RUNGEN_FILTER_HEADER="\"${D}/stencil_chain.h\"" -I ../../include ../../tools/RunGenMain.cpp ../../tools/RunGenStubs.cpp  ${D}/*.a -o ${D}/bench -ljpeg -ldl -lpthread -lz -lpng    
}

benchmark_sample() {
    D=${1}
    HL_NUM_THREADS=16 numactl -N0 ${D}/bench --output_extents=estimate --default_input_buffers=random:0:estimate --benchmarks=all --benchmark_min_time=1 > ${D}/bench.txt

    # Add the runtime, pipeline id, and schedule id to the feature file
    R=$(cut -d' ' -f8 < ${D}/bench.txt)
    P=0
    S=$2
    ./augment_sample ${D}/sample.sample $R $P $S
}

make -C ../../ distrib -j
make bin/stencil_chain.generator

# TODO: Add makefile targets for the following tools
c++ ../../tools/augment_sample.cpp -o augment_sample
c++ ../../tools/train_cost_model.cpp -std=c++11 -I ../../src -L ../../bin -lHalide -o train_cost_model

BATCH_SIZE=80

# Don't clobber existing samples
FIRST=$(ls samples | cut -d_ -f2 | sort -n | tail -n1)

for ((i=$((FIRST+1));i<1000000;i++)); do
    # Compile a batch of samples using the generator
    DIR=${PWD}/samples/batch_${i}

    for ((b=0;b<${BATCH_SIZE};b++)); do
        echo Compiling sample $b
        S=$(printf "%d%02d" $i $b)
        make_sample "${DIR}/${b}" $S &
    done
    wait
    
    # benchmark them using rungen
    for ((b=0;b<${BATCH_SIZE};b++)); do
        echo Benchmarking sample $b
        S=$(printf "%d%02d" $i $b)
        benchmark_sample "${DIR}/${b}" $S
    done
    
    # retrain model weights on all samples seen so far
    echo Retraining model...
    find samples | grep sample$ | HL_WEIGHTS_DIR=weights LD_LIBRARY_PATH=../../bin ./train_cost_model 1000
    
done
