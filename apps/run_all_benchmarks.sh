BENCHMARKS="bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain blur unsharp harris interpolate max_filter hist conv_layer"
# resnet matmul

# Set EXPLORE_PARAMETERS to 1 if you want to run a script that explores
# different architecture parameters.
EXPLORE_PARAMETERS=0


for b in $BENCHMARKS; do
	echo "Running benchmarks $b"
	if [ "${EXPLORE_PARAMETERS}" -ne 0 ]; then
		./find_gpu_parameters.sh $b
	else
		./run_one_benchmark.sh $b
	fi
done
