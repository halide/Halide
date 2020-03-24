#!/bin/bash

if [ $# -ne 1 ]; then
    echo "Usage: $0 samples_dir"
    exit
fi

source $(dirname $0)/utils.sh

find_halide HALIDE_ROOT

SAMPLES_DIR=${1}

APPS="bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate_generator conv_layer cuda_mat_mul iir_blur_generator bgu"

echo "App, Average Greedy Autoschedule Time (s), Average Beam Search Autoschedule Time (s), Average Greedy Compile Time (s), Average Beam Search Total Compile Time (s), Average Number of States Added (Greedy), Average Number of States Added (Beam Search), Average Number of Featurizations Computed (Greedy), Average Number of Featurizations Computed (Beam Search), Average Number of Schedules Enqueued by Cost Model (Greedy), Average Number of Schedules Enqueued by Cost Model (Beam Search), Average Number of Memoization Hits (Greedy), Average Number of Memoization Misses (Greedy), Average Number of Memoization Hits (Beam Search), Average Number of Memoization Misses (Beam Search), Average Featurization Time (Greedy) (ms), Average Featurization Time (Beam Search) (ms), Average Cost Model Evaluation Time (Greedy) (ms), Average Cost Model Evaluation Time (Beam Search) (ms), Average Number of Tilings Generated (Greedy), Average Number of Tilings Accepted (Greedy), Average Number of Tilings Generated (Beam Search), Average Number of Tilings Accepted (Beam Search), Average Time Per Batch (s), Best Schedule Found (ms)"

for app in $APPS; do
    FILE="${HALIDE_ROOT}/apps/${app}/${SAMPLES_DIR}/autotune_out.txt"

    if [ ! -f ${FILE} ]; then
        echo "$app, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?"
        continue
    fi

    printf "$app"
    grep "Average greedy autoschedule time" ${FILE} | tail -n 1 | awk '{if ($(NF - 1) == "-nan") printf(", ?"); else printf(", %f", $(NF - 1));}'
    grep "Average beam search autoschedule time" ${FILE} | tail -n 1 | awk '{if ($(NF - 1) == "-nan") printf(", ?"); else printf(", %f", $(NF - 1));}'
    grep "Average greedy compile time" ${FILE} | tail -n 1 | awk '{if ($(NF - 1) == "-nan") printf(", ?"); else printf(", %f", $(NF - 1));}'
    grep "Average beam search compile time" ${FILE} | tail -n 1 | awk '{if ($(NF - 1) == "-nan") printf(", ?"); else printf(", %f", $(NF - 1));}'

    grep "Average number of states added (greedy)" ${FILE} | tail -n 1 | awk '{if ($NF == "-nan") printf(", ?"); else printf(", %d", $NF);}'
    grep "Average number of states added (beam search)" ${FILE} | tail -n 1 | awk '{if ($NF == "-nan") printf(", ?"); else printf(", %d", $NF);}'
    grep "Average number of featurizations computed (greedy)" ${FILE} | tail -n 1 | awk '{if ($NF == "-nan") printf(", ?"); else printf(", %d", $NF);}'
    grep "Average number of featurizations computed (beam search)" ${FILE} | tail -n 1 | awk '{if ($NF == "-nan") printf(", ?"); else printf(", %d", $NF);}'
    grep "Average number of schedules evaluated by cost model (greedy)" ${FILE} | tail -n 1 | awk '{if ($NF == "-nan") printf(", ?"); else printf(", %d", $NF);}'
    grep "Average number of schedules evaluated by cost model (beam search)" ${FILE} | tail -n 1 | awk '{if ($NF == "-nan") printf(", ?"); else printf(", %d", $NF);}'

    grep "Average number of memoization hits (greedy)" ${FILE} | tail -n 1 | awk '{if ($NF == "-2147483647") printf(", ?"); else printf(", %d", $NF);}'
    grep "Average number of memoization misses (greedy)" ${FILE} | tail -n 1 | awk '{if ($NF == "-2147483647") printf(", ?"); else printf(", %d", $NF);}'
    grep "Average number of memoization hits (beam search)" ${FILE} | tail -n 1 | awk '{if ($NF == "-2147483647") printf(", ?"); else printf(", %d", $NF);}'
    grep "Average number of memoization misses (beam search)" ${FILE} | tail -n 1 | awk '{if ($NF == "-2147483647") printf(", ?"); else printf(", %d", $NF);}'

    grep "Average featurization time (greedy)" ${FILE} | tail -n 1 | awk '{if ($NF == "-nan") printf(", ?"); else printf(", %f", $NF);}'
    grep "Average featurization time (beam search)" ${FILE} | tail -n 1 | awk '{if ($NF == "-nan") printf(", ?"); else printf(", %f", $NF);}'
    grep "Average cost model evaluation time (greedy)" ${FILE} | tail -n 1 | awk '{if ($NF == "-nan") printf(", ?"); else printf(", %f", $NF);}'
    grep "Average cost model evaluation time (beam search)" ${FILE} | tail -n 1 | awk '{if ($NF == "-nan") printf(", ?"); else printf(", %f", $NF);}'

    grep "Average number of tilings generated (greedy)" ${FILE} | tail -n 1 | awk '{if ($NF == "-2147483647") printf(", ?"); else printf(", %d", $NF);}'
    grep "Average number of tilings accepted (greedy)" ${FILE} | tail -n 1 | awk '{if ($NF == "-2147483647") printf(", ?"); else printf(", %d", $NF);}'
    grep "Average number of tilings generated (beam search)" ${FILE} | tail -n 1 | awk '{if ($NF == "-2147483647") printf(", ?"); else printf(", %d", $NF);}'
    grep "Average number of tilings accepted (beam search)" ${FILE} | tail -n 1 | awk '{if ($NF == "-2147483647") printf(", ?"); else printf(", %d", $NF);}'

    grep "Batch" ${FILE} | awk '{sum += $4}; END{if (NR == 0) printf(", ?"); else printf(", %f", sum / NR);}'

    output=$(grep "Best runtime" ${FILE})
    if [[ -n $output ]]; then
        grep "Best runtime" ${FILE} | tail -n 1 | awk '{if (NR == 0) printf(", ?\n"); else printf(", %f\n", $4);}'
    else
        echo ", ?"
    fi
done

echo
