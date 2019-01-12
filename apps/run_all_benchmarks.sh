BENCHMARKS="bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate_generator conv_layer iir_blur_generator bgu0"
# resnet_50 cuda_mat_mul

# Set EXPLORE_PARAMETERS to 1 if you want to run a script that explores
# different architecture parameters.
EXPLORE_PARAMETERS=0


for b in $BENCHMARKS; do
	echo "Running benchmarks $b"
	if [ "${EXPLORE_PARAMETERS}" -ne 0 ]; then
		./explore_gpu_parameters.sh $b
	else
		./run_one_benchmark.sh $b
	fi
done
