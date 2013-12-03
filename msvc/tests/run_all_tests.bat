echo Running test argmax
test_argmax.exe
if not errorlevel 0 exit 1
echo Running test assertion_failure_in_parallel_for
test_assertion_failure_in_parallel_for.exe
if not errorlevel 0 exit 1
echo Running test autotune_bug
test_autotune_bug.exe
if not errorlevel 0 exit 1
echo Running test autotune_bug_2
test_autotune_bug_2.exe
if not errorlevel 0 exit 1
echo Running test autotune_bug_3
test_autotune_bug_3.exe
if not errorlevel 0 exit 1
echo Running test autotune_bug_4
test_autotune_bug_4.exe
if not errorlevel 0 exit 1
echo Running test bad_elem_size
test_bad_elem_size.exe
if not errorlevel 0 exit 1
echo Running test bitwise_ops
test_bitwise_ops.exe
if not errorlevel 0 exit 1
echo Running test bit_counting
test_bit_counting.exe
if not errorlevel 0 exit 1
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
echo Running test bounds_of_abs
test_bounds_of_abs.exe
if not errorlevel 0 exit 1
echo Running test bounds_of_cast
test_bounds_of_cast.exe
if not errorlevel 0 exit 1
echo Running test bounds_query
test_bounds_query.exe
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
echo Running test circular_reference_leak
test_circular_reference_leak.exe
if not errorlevel 0 exit 1
echo Running test code_explosion
test_code_explosion.exe
if not errorlevel 0 exit 1
echo Running test compare_vars
test_compare_vars.exe
if not errorlevel 0 exit 1
echo Running test compile_to_lowered_stmt
test_compile_to_lowered_stmt.exe
if not errorlevel 0 exit 1
echo Running test computed_index
test_computed_index.exe
if not errorlevel 0 exit 1
echo Running test constant_type
test_constant_type.exe
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
echo Running test div_mod
test_div_mod.exe
if not errorlevel 0 exit 1
echo Running test dynamic_reduction_bounds
test_dynamic_reduction_bounds.exe
if not errorlevel 0 exit 1
echo Running test evil_parallel_reductions
test_evil_parallel_reductions.exe
if not errorlevel 0 exit 1
echo Running test expanding_reduction
test_expanding_reduction.exe
if not errorlevel 0 exit 1
echo Running test extern_consumer
test_extern_consumer.exe
if not errorlevel 0 exit 1
echo Running test extern_error
test_extern_error.exe
if not errorlevel 0 exit 1
echo Running test extern_producer
test_extern_producer.exe
if not errorlevel 0 exit 1
echo Running test extern_sort
test_extern_sort.exe
if not errorlevel 0 exit 1
echo Running test extern_stage
test_extern_stage.exe
if not errorlevel 0 exit 1
echo Running test fibonacci
test_fibonacci.exe
if not errorlevel 0 exit 1
echo Running test fuse
test_fuse.exe
if not errorlevel 0 exit 1
echo Running test gameoflife
test_gameoflife.exe
if not errorlevel 0 exit 1
echo Running test gpu_large_alloc
test_gpu_large_alloc.exe
if not errorlevel 0 exit 1
echo Running test gpu_multi_kernel
test_gpu_multi_kernel.exe
if not errorlevel 0 exit 1
echo Running test handle
test_handle.exe
if not errorlevel 0 exit 1
echo Running test heap_cleanup
test_heap_cleanup.exe
if not errorlevel 0 exit 1
echo Running test hello_gpu
test_hello_gpu.exe
if not errorlevel 0 exit 1
echo Running test histogram
test_histogram.exe
if not errorlevel 0 exit 1
echo Running test histogram_equalize
test_histogram_equalize.exe
if not errorlevel 0 exit 1
echo Running test image_of_lists
test_image_of_lists.exe
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
echo Running test integer_powers
test_integer_powers.exe
if not errorlevel 0 exit 1
echo Running test interleave
test_interleave.exe
if not errorlevel 0 exit 1
echo Running test lambda
test_lambda.exe
if not errorlevel 0 exit 1
echo Running test lazy_convolution
test_lazy_convolution.exe
if not errorlevel 0 exit 1
echo Running test lerp
test_lerp.exe
if not errorlevel 0 exit 1
echo Running test logical
test_logical.exe
if not errorlevel 0 exit 1
echo Running test loop_invariant_extern_calls
test_loop_invariant_extern_calls.exe
if not errorlevel 0 exit 1
echo Running test many_dimensions
test_many_dimensions.exe
if not errorlevel 0 exit 1
echo Running test math
test_math.exe
if not errorlevel 0 exit 1
echo Running test mod
test_mod.exe
if not errorlevel 0 exit 1
echo Running test multiple_outputs
test_multiple_outputs.exe
if not errorlevel 0 exit 1
echo Running test multi_output_pipeline_with_bad_sizes
test_multi_output_pipeline_with_bad_sizes.exe
if not errorlevel 0 exit 1
echo Running test multi_pass_reduction
test_multi_pass_reduction.exe
if not errorlevel 0 exit 1
echo Running test newtons_method
test_newtons_method.exe
if not errorlevel 0 exit 1
echo Running test obscure_image_references
test_obscure_image_references.exe
if not errorlevel 0 exit 1
echo Running test oddly_sized_output
test_oddly_sized_output.exe
if not errorlevel 0 exit 1
echo Running test out_of_memory
test_out_of_memory.exe
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
echo Running test parallel_reductions
test_parallel_reductions.exe
if not errorlevel 0 exit 1
echo Running test param
test_param.exe
if not errorlevel 0 exit 1
echo Running test parameter_constraints
test_parameter_constraints.exe
if not errorlevel 0 exit 1
echo Running test partial_application
test_partial_application.exe
if not errorlevel 0 exit 1
echo Running test process_some_tiles
test_process_some_tiles.exe
if not errorlevel 0 exit 1
echo Running test realize_over_shifted_domain
test_realize_over_shifted_domain.exe
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
echo Running test shifted_image
test_shifted_image.exe
if not errorlevel 0 exit 1
echo Running test side_effects
test_side_effects.exe
if not errorlevel 0 exit 1
echo Running test skip_stages
test_skip_stages.exe
if not errorlevel 0 exit 1
echo Running test sliding_backwards
test_sliding_backwards.exe
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
echo Running test tracing
test_tracing.exe
if not errorlevel 0 exit 1
echo Running test tracing_stack
test_tracing_stack.exe
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
echo Running test vectorized_initialization
test_vectorized_initialization.exe
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
