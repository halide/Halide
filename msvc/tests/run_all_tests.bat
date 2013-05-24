echo Running test bound
test_bound.exe
if not errorlevel 0 exit 1
echo Running test bounds
test_bounds.exe
if not errorlevel 0 exit 1
echo Running test bounds_inference
test_bounds_inference.exe
if not errorlevel 0 exit 1
echo Running test bounds_inference_chunk
test_bounds_inference_chunk.exe
if not errorlevel 0 exit 1
echo Running test bounds_inference_complex
test_bounds_inference_complex.exe
if not errorlevel 0 exit 1
echo Running test cast
test_cast.exe
if not errorlevel 0 exit 1
echo Running test chunk
test_chunk.exe
if not errorlevel 0 exit 1
echo Running test chunk_sharing
test_chunk_sharing.exe
if not errorlevel 0 exit 1
echo Running test compare_vars
test_compare_vars.exe
if not errorlevel 0 exit 1
echo Running test constraints
test_constraints.exe
if not errorlevel 0 exit 1
echo Running test convolution
test_convolution.exe
if not errorlevel 0 exit 1
echo Running test custom_allocator
test_custom_allocator.exe
if not errorlevel 0 exit 1
echo Running test c_function
test_c_function.exe
if not errorlevel 0 exit 1
echo Running test debug_to_file
test_debug_to_file.exe
if not errorlevel 0 exit 1
echo Running test fibonacci
test_fibonacci.exe
if not errorlevel 0 exit 1
echo Running test gameoflife
test_gameoflife.exe
if not errorlevel 0 exit 1
echo Running test gpu_large_alloc
test_gpu_large_alloc.exe
if not errorlevel 0 exit 1
echo Running test heap_cleanup
test_heap_cleanup.exe
if not errorlevel 0 exit 1
echo Running test histogram
test_histogram.exe
if not errorlevel 0 exit 1
echo Running test histogram_equalize
test_histogram_equalize.exe
if not errorlevel 0 exit 1
echo Running test implicit_args
test_implicit_args.exe
if not errorlevel 0 exit 1
echo Running test inline_reduction
test_inline_reduction.exe
if not errorlevel 0 exit 1
echo Running test input_image_bounds_check
test_input_image_bounds_check.exe
if not errorlevel 0 exit 1
echo Running test interleave
test_interleave.exe
if not errorlevel 0 exit 1
echo Running test internal
test_internal.exe
if not errorlevel 0 exit 1
echo Running test jit_stress
test_jit_stress.exe
if not errorlevel 0 exit 1
echo Running test lambda
test_lambda.exe
if not errorlevel 0 exit 1
echo Running test logical
test_logical.exe
if not errorlevel 0 exit 1
echo Running test mod
test_mod.exe
if not errorlevel 0 exit 1
echo Running test obscure_image_references
test_obscure_image_references.exe
if not errorlevel 0 exit 1
echo Running test parallel
test_parallel.exe
if not errorlevel 0 exit 1
echo Running test parallel_alloc
test_parallel_alloc.exe
if not errorlevel 0 exit 1
echo Running test parallel_nested
test_parallel_nested.exe
if not errorlevel 0 exit 1
echo Running test parallel_performance
test_parallel_performance.exe
if not errorlevel 0 exit 1
echo Running test parallel_reductions
test_parallel_reductions.exe
if not errorlevel 0 exit 1
echo Running test param
test_param.exe
if not errorlevel 0 exit 1
echo Running test partial_application
test_partial_application.exe
if not errorlevel 0 exit 1
echo Running test reduction_schedule
test_reduction_schedule.exe
if not errorlevel 0 exit 1
echo Running test reduction_subregion
test_reduction_subregion.exe
if not errorlevel 0 exit 1
echo Running test reorder_storage
test_reorder_storage.exe
if not errorlevel 0 exit 1
echo Running test scatter
test_scatter.exe
if not errorlevel 0 exit 1
echo Running test side_effects
test_side_effects.exe
if not errorlevel 0 exit 1
echo Running test simd_op_check
test_simd_op_check.exe
if not errorlevel 0 exit 1
echo Running test sliding_window
test_sliding_window.exe
if not errorlevel 0 exit 1
echo Running test split_reuse_inner_name_bug
test_split_reuse_inner_name_bug.exe
if not errorlevel 0 exit 1
echo Running test split_store_compute
test_split_store_compute.exe
if not errorlevel 0 exit 1
echo Running test storage_folding
test_storage_folding.exe
if not errorlevel 0 exit 1
echo Running test two_vector_args
test_two_vector_args.exe
if not errorlevel 0 exit 1
echo Running test unique_func_image
test_unique_func_image.exe
if not errorlevel 0 exit 1
echo Running test unrolled_reduction
test_unrolled_reduction.exe
if not errorlevel 0 exit 1
echo Running test update_chunk
test_update_chunk.exe
if not errorlevel 0 exit 1
echo Running test vectorize
test_vectorize.exe
if not errorlevel 0 exit 1
echo Running test vector_bounds_inference
test_vector_bounds_inference.exe
if not errorlevel 0 exit 1
echo Running test vector_cast
test_vector_cast.exe
if not errorlevel 0 exit 1
echo Running test vector_extern
test_vector_extern.exe
if not errorlevel 0 exit 1
echo Running test vector_math
test_vector_math.exe
if not errorlevel 0 exit 1
