#!/bin/bash

#make -C ../../ clean
#make -C ../../ distrib -j32
# APP_LIST="harris hist local_laplacian unsharp bilateral_grid camera_pipe nl_means stencil_chain iir_blur interpolate max_filter lens_blur"
APP_LIST="bgu camera_pipe conv_layer depthwise_separable_conv harris hist iir_blur interpolate lens_blur local_laplacian max_filter stencil_chain unsharp"

for app in $APP_LIST; do
    echo 
    echo '***********' $app
    echo
    pushd ${app}
    echo "Total failed unrolls (ours):"
    cat results/total_failed_unrolls.txt
    echo "Total failed unrolls (base):"
    cat results_baseline/total_failed_unrolls.txt

    echo "Total malloc calls (ours):"
    cat results/total_malloc_calls.txt
    echo "Total malloc calls (base):"
    cat results_baseline/total_malloc_calls.txt

    echo "Total memory (ours):"
    cat results/total_memory.txt
    echo "Total memory (base):"
    cat results_baseline/total_memory.txt

    popd
done
