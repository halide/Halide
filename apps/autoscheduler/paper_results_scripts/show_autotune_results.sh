HALIDE=$(dirname $0)/../../..
echo "Using Halide in " $HALIDE

APPS="bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate_generator conv_layer mat_mul_generator iir_blur_generator bgu"

echo "Best time including all random samples"

for app in ${APPS}; do
    echo $app
    S=$(cat ${HALIDE}/apps/${app}/samples/*/*/bench.txt | cut -d' ' -f8 | sort -n | head -n1)
    echo "$S * 1000" | bc
done

# For resnet we need to sum over the blocks
echo resnet_50_blockwise
S=$(for ((block=0;block<16;block++)); do
        cat ${HALIDE}/apps/resnet_50_blockwise/samples/batch_*_${block}/*/bench.txt | cut -d' ' -f8 | sort -n | head -n1
    done | paste -sd+ | bc)
echo "$S * 1000" | bc


echo "Beam search on final set of weights"

for app in ${APPS}; do
    echo $app
    S=$(ls -t ${HALIDE}/apps/${app}/samples/*/0/bench.txt | head -n1 | xargs cat | cut -d' ' -f8)
    echo "$S * 1000" | bc
done

# For resnet we need to sum over the blocks
echo resnet_50_blockwise
S=$(for ((block=0;block<16;block++)); do
        ls -t ${HALIDE}/apps/resnet_50_blockwise/samples/batch_*_${block}/0/bench.txt | head -n1 | xargs cat | cut -d' ' -f8
    done | paste -sd+ | bc)
echo "$S * 1000" | bc
