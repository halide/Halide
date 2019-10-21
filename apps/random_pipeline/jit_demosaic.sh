#!/bin/bash
JOB_ID=$1

PIPELINE_SEED=1
PIPELINE_STAGES=2

HL_TARGET=x86-64-avx2-disable_llvm_loop_vectorize-disable_llvm_loop_unroll

BIN=./bin

make ./bin/new_generator.generator

SAMPLES_DIR=/mnt/ilcompf8d1/user/kcma/b_gr_2stages_${JOB_ID}/

rm -rf ${SAMPLES_DIR}
mkdir -p ${SAMPLES_DIR}

# IMAGES_LIST_FILE=${SAMPLES_DIR}/images_list.txt
DATA_DIR=/mnt/ilcompf8d1/user/kcma/bayer_data/
LOCAL_IMAGES_FILE=$TMPDIR/images_list.txt

start=$(date +%s%N | cut -b1-13)

# copy data into local working directory
LOCAL_DATA_DIR=$TMPDIR/bayer_data
cp -r $DATA_DIR $LOCAL_DATA_DIR

# create a list of local data files
for image_dir in "${LOCAL_DATA_DIR}"/*
do
  echo ${image_dir} >> ${LOCAL_IMAGES_FILE}
done

NUM_IMAGES=$(cat ${LOCAL_IMAGES_FILE} | wc -l)

end=$(date +%s%N | cut -b1-13)
(( diff = $end - $start ))
echo "TIME TO SET UP DATA: $diff"

# how many pipelines to generate
NUM_SAMPLES=3000
CORES=8

loop_start=$(date +%s%N | cut -b1-13)

for ((b=0;b<${CORES};b++)); do
  DIR=${SAMPLES_DIR}/batch_${b}
  mkdir -p ${DIR}
  (( START_SEED=${JOB_ID} * ${CORES} * ${NUM_SAMPLES} + ${b} * ${NUM_SAMPLES} ))

  #start=$(date +%s%N | cut -b1-13)
  # compile code that will run generator and eval on a dataset of images and then run it 
  g++ -w -std=c++11  -I ../../distrib/include/ -I ../../distrib/tools/ -I ./ -g -Wall jit_demosaic.cpp ../../distrib/lib/libHalide.a -o ${DIR}/gen_demosaic_pipes -ldl -lpthread -lz -ltinfo -fopenmp && ${DIR}/gen_demosaic_pipes ${LOCAL_IMAGES_FILE} ${DIR} ${NUM_IMAGES} ${NUM_SAMPLES} ${START_SEED} &
  #end=$(date +%s%N | cut -b1-13)
  #(( diff = $end - $start ))
  #echo "TIME TO COMPILE CPP: $diff"

  pids[${b}]=$!
done

# wait for all parallel runs to finish
for ((b=0;b<${CORES};b++)); do
  echo "running gen_demosaic_pipes on core $b" 
  wait ${pids[${b}]}
done

end=$(date +%s%N | cut -b1-13)
(( diff = $end - $loop_start ))
echo "time to compile and eval ${NUM_SAMPLES} pipes on ${CORES} cores: $diff"

