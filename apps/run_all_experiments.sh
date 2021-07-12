#!/bin/bash

#make -C ../../ clean
#make -C ../../ distrib -j32
# APP_LIST="harris hist local_laplacian unsharp bilateral_grid camera_pipe nl_means stencil_chain iir_blur interpolate max_filter lens_blur resnet_50 resize"
APP_LIST="harris "

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
