#!/bin/bash

# BENCHMARKS="add average_pool blur3x3 camera_pipe_test fully_connected gaussian3x3 gaussian5x5 gaussian7x7 l2norm matmul median3x3 mul softmax sobel3x3"
BENCHMARKS="dilate3x3 conv3x3_a16 conv3x3_a32 depthwise_conv"

# mean
# max_pool
# conv_nn

for value in $BENCHMARKS
do
    echo "Testing $value..."
    cd $value
    make clean; make test
    cd ..
done