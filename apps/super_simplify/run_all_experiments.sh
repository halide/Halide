#!/bin/bash

make -C ../../ clean
make -C ../../ distrib -j32

for app in harris local_laplacian unsharp bilateral_grid camera_pipe nl_means stencil_chain iir_blur interpolate max_filter lens_blur resnet_50 resize; do
    pushd ../${app}
    make clean
    rm -rf results results_baseline
    bash ../super_simplify/run_experiment.sh 0 64
    popd
done
