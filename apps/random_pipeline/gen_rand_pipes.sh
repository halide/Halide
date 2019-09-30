#!/bin/bash
PIPELINE_SEED=1
PIPELINE_STAGES=1

HL_TARGET=x86-64-avx2-disable_llvm_loop_vectorize-disable_llvm_loop_unroll

BIN=./bin

make ./bin/new_generator.generator

SAMPLES=${PWD}/samples
mkdir -p ${SAMPLES}

for ((i=0;i<3;i++)); do
	DIR=${SAMPLES}/sample_${i}
  mkdir -p ${DIR}

	# run generator to compile new random pipeline
	${BIN}/new_generator.generator -g random_pipeline_inference -o ${DIR} -e static_library,h,stmt,assembly,registration target=${HL_TARGET} auto_schedule=false max_stages=${PIPELINE_STAGES} seed=${i} num_input_buffers=4 num_output_buffers=1 2> ${DIR}/compile_log_stderr.txt > ${DIR}/compile_log_stdout.txt

	# compile test to use new pipeline 
	g++ -std=c++11  -I ../../distrib/include/ -I ../../distrib/tools/ -g -Wall -I ${DIR} run_demosaic.cpp ${DIR}/random_pipeline_inference.a -o ${DIR}/test_gen -ldl -lpthread -lz -fopenmp
	# run the pipeline output the loss
	${DIR}/test_gen
done
