#!/bin/bash

# make -C ../../ clean
# make -C ../../ distrib -j32
# APP_LIST="hist harris interpolate lens_blur max_filter unsharp stencil_chain camera_pipe iir_blur depthwise_separable_conv conv_layer bgu"
# APP_LIST="camera_pipe iir_blur depthwise_separable_conv conv_layer bgu"
# APP_LIST="hist harris lens_blur max_filter unsharp stencil_chain camera_pipe iir_blur depthwise_separable_conv conv_layer bgu"

APP_LIST="hist harris lens_blur max_filter unsharp camera_pipe iir_blur depthwise_separable_conv conv_layer bgu stencil_chain"
# APP_LIST="camera_pipe iir_blur depthwise_separable_conv conv_layer bgu stencil_chain"

for app in $APP_LIST; do
    echo 
    echo '***********' $app
    echo
    pushd ${app}
    make clean
    rm -rf results results_baseline
    bash ../run_experiment.sh 256 266
    popd
done
