#!/bin/bash
JOB_ID=$1
PIPELINE_SEED=1
PIPELINE_STAGES=1

HL_TARGET=x86-64-avx2-disable_llvm_loop_vectorize-disable_llvm_loop_unroll

BIN=./bin

make ./bin/new_generator.generator

SAMPLES_DIR=/mnt/ilcompf8d1/user/kcma/samples_v3_${JOB_ID}/
rm -rf ${SAMPLES_DIR}
mkdir -p ${SAMPLES_DIR}

IMAGES_LIST_FILE=${SAMPLES_DIR}/images_list.txt
DATA_DIR=/mnt/ilcompf8d1/user/kcma/bayer_data/

# create a list of data files
rm -f ${IMAGES_LIST_FILE}
for image_dir in "${DATA_DIR}"/*
do
  echo ${image_dir} >> ${IMAGES_LIST_FILE}
done

NUM_IMAGES=$(ls ${DATA_DIR} | wc -l)

# how many pipelines to generate
NUM_SAMPLES=3

for ((i=0;i<${NUM_SAMPLES};i++)); do
  (( SEED = ${NUM_SAMPLES} * ${JOB_ID} + $i ))
  echo ${SEED}
  DIR=${SAMPLES_DIR}/sample_${i}
  mkdir -p ${DIR}

  # run generator to compile new random pipeline

  # compile test to use new pipeline 
  g++ -w -std=c++11  -I ../../distrib/include/ -I ../../distrib/tools/ -g -Wall -I ${DIR} run_demosaic.cpp new_generator.cpp ../../distrib/lib/libHalide.a -o ${DIR}/gen_demosaic_pipes -ldl -lpthread -lz -fopenmp
  # run the jitted generator to generate pipelines and evaluate their losses
  ${DIR}/gen_demosaic_pipes ${IMAGES_LIST_FILE} ${DIR} ${NUM_IMAGES} ${NUM_SAMPLES} ${JOB_ID}
done

best_val=10000000000000
best_sample=0

for ((i=0;i<${NUM_SAMPLES};i++)); do
  DIR=${SAMPLES_DIR}/sample_${i}
  LOSS_FILE=${DIR}/loss.txt
  value=$(<${LOSS_FILE})
  echo "$value"

  if (( $value < $best_val)); then
    best_val=$value
    best_sample=$i
  fi
done

echo "best sample: $best_sample"
echo "with loss: $best_val"
