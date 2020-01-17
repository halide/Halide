#!/bin/bash

PIPELINE_STAGES=1

HL_TARGET=x86-64-avx2-disable_llvm_loop_vectorize-disable_llvm_loop_unroll

BIN=./bin

SAMPLES_DIR=/mnt/ilcompf8d1/user/kcma/jit_ref_b_gr_demosaic/
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

start=$(date +%s%N | cut -b1-13)

DIR=${SAMPLES_DIR}
mkdir -p ${DIR}

# compile code that will run generator and eval on a dataset of images and then run it 
g++ -w -std=c++11  -I ../../distrib/include/ -I ../../distrib/tools/ -I ./ -g -Wall jit_ref_demosaic.cpp ../../distrib/lib/libHalide.a -o ${DIR}/gen_demosaic_pipes -ldl -lpthread -lz -ltinfo -fopenmp -D B_GR && \
${DIR}/gen_demosaic_pipes ${IMAGES_LIST_FILE} ${DIR} ${NUM_IMAGES}

end=$(date +%s%N | cut -b1-13)
(( diff = $end - $start ))

echo "time to compile and eval ref pipeline: $diff"

