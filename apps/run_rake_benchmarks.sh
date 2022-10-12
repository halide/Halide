#!/bin/bash

BENCHMARKS="add average_pool blur3x3 camera_pipe_test conv3x3_a16 conv3x3_a32 depthwise_conv fully_connected gaussian3x3 gaussian5x5 gaussian7x7 l2norm matmul mul softmax sobel3x3"

TARGET=$1
# mean
# conv_nn

for value in $BENCHMARKS
do
    echo "Testing $value..."
    cd $value
    make clean; make $TARGET &> benchmark_$TARGET.out
    cd ..
done

wait
