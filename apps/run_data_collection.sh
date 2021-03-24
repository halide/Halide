#!/bin/bash

# Round 1
# APP_LIST="harris hist iir_blur max_filter bilateral_grid"
# Round 2
# APP_LIST="unsharp depthwise_separable_conv nl_means lens_blur stencil_chain local_laplacian"
# For testing
# APP_LIST="hist"
# APP_LIST="harris hist iir_blur max_filter nl_means unsharp bilateral_grid stencil_chain depthwise_separable_conv"
# Hard apps:
# APP_LIST="bgu lens_blur local_laplacian resnet50"

# APP_LIST="harris hist"
APP_LIST="harris hist iir_blur max_filter unsharp bilateral_grid stencil_chain depthwise_separable_conv"

ROUND=22

# 3: choose_any_random_child
# 4: choose_weighted_random_child
# 5: expansion
# 6: #3 repeat with harder apps
# 7: mess with iteration count for #3
# 8: same as 7, but half the initial iteration count
# 9: same as 3, but half the initial iteration count
# 10: try lower random dropouts
# 11: #10 + less iterations

# March 10th:
# 12: Try weighted sampling for MCTS decisions (.10) [killed at nl_means]
# 13: try #12 without nl_means
# 14: try #13 with exploration weight 0.05
# 15: try #13 with exploration weight 0.20
# 16: #5 but gather expansion vs. exploitation data.
# 17: try #13 but with a constant added to exploration of 0.05
# 18: try #17 but with MCTS exploration (#5) (n_i = .05*b + 16)
# 19: try 18 but fix a possible bug??
# 20: try 19 but with choose_any_random_child (n_i = .05*b + 8)
# 20: sample best n_i children per decision (n_i = .05*b + 8)

# 22: smart sampling of children (n_i = .15*b + 1)
one_iter() {
    make clean; export HL_RANDOM_DROPOUT=$1; make test &> out_mcts_$1_def_round$ROUND.log
    echo $(pwd) $1
    grep "Found Pipeline with cost:" out_mcts_$1_def_round$ROUND.log
    grep "ms" out_mcts_$1_def_round$ROUND.log
    sleep 10
}

collect_data() {
    cp ../support/Makefile-mcts.inc ../support/Makefile.inc
    one_iter "50";
    one_iter "25";
    one_iter "10";
    one_iter "5";
    one_iter "2";
    one_iter "1";
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