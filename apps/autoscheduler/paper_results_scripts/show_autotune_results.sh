autoscheduler="$1"

if [ "$RETRAIN" == "true" ]; then
    results="autotune/retrain/$autoscheduler"
elif [ "$RETRAIN" == "false" ]; then
    results="autotune/noretrain/$autoscheduler"
else
    echo You must set RETRAIN env variable to \"true\" or \"false\"
    exit 1
fi
mkdir -p $results

HALIDE=$(dirname $0)/../../..
echo "Using Halide in " $HALIDE

# APPS="bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate_generator conv_layer mat_mul_generator iir_blur_generator bgu"
APPS="bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate conv_layer iir_blur bgu" # Missing mat_mul_generator and resnet_50_blockwise

echo "Best time including all random samples"

echo "test,time" > "$results/results.csv"

for app in ${APPS}; do
    echo $app
    S=$(cat ${HALIDE}/apps/${app}/samples/*/*/bench.txt | cut -d' ' -f8 | sed '/^$/d' | sort -n | head -n1)

    if [ $? -eq 0 ]; then
        echo "$S * 1000" | bc
        echo "$app,$S" >> "$results/results.csv"
    else
        echo Failed to extract results
        echo "$app," >> "$results/results.csv"
    fi
done

# For resnet we need to sum over the blocks
# echo resnet_50
# S=$(for ((block=0;block<16;block++)); do
#         cat ${HALIDE}/apps/resnet_50/samples/batch_*_${block}/*/bench.txt | cut -d' ' -f8 | sort -n | head -n1
#     done | paste -sd+ | bc)
# echo "$S * 1000" | bc


echo "Beam search on final set of weights"

for app in ${APPS}; do
    echo $app
    S=$(ls -t ${HALIDE}/apps/${app}/samples/*/0/bench.txt | head -n1 | xargs cat | cut -d' ' -f8)
    echo "$S * 1000" | bc
done

# For resnet we need to sum over the blocks
# echo resnet_50
# S=$(for ((block=0;block<16;block++)); do
#         ls -t ${HALIDE}/apps/resnet_50/samples/batch_*_${block}/0/bench.txt | head -n1 | xargs cat | cut -d' ' -f8
#     done | paste -sd+ | bc)
# echo "$S * 1000" | bc
