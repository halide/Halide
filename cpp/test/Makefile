all: test_bound \
test_bounds \
test_bounds_inference \
test_bounds_inference_chunk \
test_bounds_inference_complex \
test_c_function \
test_chunk \
test_chunk_sharing \
test_convolution \
test_fibonacci \
test_footprint \
test_gameoflife \
test_gpu_large_alloc \
test_histogram \
test_histogram_equalize \
test_input_image_bounds_check \
test_jit_stress \
test_logical \
test_mod \
test_parallel \
test_parallel_alloc \
test_parallel_nested \
test_parallel_performance \
test_partial_application \
test_reduction \
test_reduction_schedule \
test_reduction_subregion \
test_rehydrate \
test_side_effects \
test_simd_op_check \
test_split_reuse_inner_name_bug \
test_split_store_compute \
test_two_vector_args \
test_uniform \
test_unrolled_reduction \
test_vector_bounds_inference \
test_vector_cast \
test_vector_extern \
test_vectorize \
test_vector_math

%/a.out: %/test.cpp 
	g++ $< -I.. -L.. -lHalide -lpthread -ldl -o $@	

test_%: %/a.out
	./$<