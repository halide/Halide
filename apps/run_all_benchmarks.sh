BENCHMARKS="bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain blur unsharp harris interpolate max_filter hist conv_layer"
# resnet matmul

for b in $BENCHMARKS; do
	echo "Running benchmarks $b"
	./find_gpu_parameters.sh $b
done
