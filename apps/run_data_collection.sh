#!/bin/bash

# Round 1
# APP_LIST="harris hist iir_blur max_filter bilateral_grid"
# Round 2
APP_LIST="unsharp depthwise_separable_conv nl_means lens_blur stencil_chain local_laplacian"

one_iter() {
    make clean; export HL_RANDOM_DROPOUT=$1; make test &> out_mcts_$1_def.log
    echo $(pwd) $1
    grep "Found Pipeline with cost:" out_mcts_$1_def.log
    grep "ms" out_mcts_$1_def.log
    sleep 10
}

collect_data() {
    cp ../support/Makefile-mcts.inc ../support/Makefile.inc
    one_iter "100";
    one_iter "75";
    one_iter "50";
    one_iter "25";
    unset HL_RANDOM_DROPOUT;
    cp ../support/Makefile-adams2019.inc ../support/Makefile.inc
    make clean; make test &> out_adams2019_def.log
    echo $(pwd) "adams2019"
    grep "Best cost:" out_adams2019_def.log
    grep "ms" out_adams2019_def.log
}

for value in $APP_LIST
do
    echo $value
    cd $value
    collect_data;
    echo $(pwd) "Finished"
    cd ..
done