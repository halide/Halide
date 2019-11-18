#!/bin/bash

for app in harris local_laplacian unsharp bilateral_grid camera_pipe nl_means stencil_chain iir_blur interpolate max_filter; do
    pushd ../${app}
    bash ../super_simplify/run_experiment.sh 0 64
    popd
done
