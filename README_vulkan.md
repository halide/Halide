# Vulkan Support for Halide

Halide supports the Khronos Vulkan framework as a compute API backend for GPU-like 
devices, and compiles directly to a binary SPIR-V representation as part of its 
code generation before submitting it to the Vulkan API. Both JIT and AOT usage 
are supported via the `vulkan` target flag (eg `HL_JIT_TARGET=host-vulkan`).

Vulkan support is actively under development, and considered *EXPERIMENTAL*
at this stage.  Basic tests are passing, but there's still work to do to
until we have adequate feature parity for production use.  

See [below](#current-status) for details on specific test cases.

# Compiling Halide w/Vulkan Support

You'll need to configure Halide and enable the cmake option TARGET_VULKAN.

For example, on Linux & OSX:

```
% cmake -G Ninja -DTARGET_VULKAN=ON -DCMAKE_BUILD_TYPE=Release -DLLVM_DIR=$LLVM_ROOT/lib/cmake/llvm -S . -B build
% cmake --build build
```

# Vulkan Runtime Environment:

Halide has no direct dependency on Vulkan for code-generation, but the runtime
requires a working Vulkan environment to run Halide generated code. Any valid 
Vulkan v1.0+ device driver should work. 

Specifically, you'll need:

-   A vendor specific Vulkan device driver
-   The generic Vulkan loader library

For AMD & NVIDIA & Intel devices, download and install the latest graphics driver 
for your platform. Vulkan support should be included.

## Windows 

AMD:
https://www.amd.com/en/technologies/vulkan

NVIDIA:
https://developer.nvidia.com/vulkan-driver

INTEL:
https://www.intel.com/content/www/us/en/download-center/home.html


## Linux 

On Ubuntu Linux, proprietary drivers can be installed via 'apt' using 
PPA's for each vendor.

For AMD:
```
$ sudo add-apt-repository ppa:oibaf/graphics-drivers
$ sudo apt update
$ sudo apt upgrade
$ sudo apt install libvulkan1 mesa-vulkan-drivers vulkan-utils
```

For NVIDIA:
```
$ sudo add-apt-repository ppa:graphics-drivers/ppa
$ sudo apt update
$ sudo apt upgrade
# - replace ### with latest driver release (eg 515)
$ sudo apt install nvidia-driver-### nvidia-settings vulkan vulkan-utils
```

Note that only valid drivers for your system should be installed since there's been 
reports of the Vulkan loader segfaulting just by having a non-supported driver present. 
Specifically, the seemingly generic `mesa-vulkan-drivers` actually includes the AMD 
graphics driver, which can cause problems if installed on an NVIDIA only system. 

## Mac

You're better off using Halide's Metal backend instead, but it is possible to run 
Vulkan apps on a Mac via the MoltenVK library:

MoltenVK:
https://github.com/KhronosGroup/MoltenVK

# Testing Your Vulkan Environment

You can validate that everything is configured correctly by running the `vulkaninfo`
app (bundled in the vulkan-utils package) to make sure your device is detected (eg):

```
$ vulkaninfo
==========
VULKANINFO
==========

Vulkan Instance Version: 1.3.224


Instance Extensions: count = 19
===============================
	...

Layers: count = 10
==================
VK_LAYER_KHRONOS_profiles (Khronos Profiles layer) Vulkan version 1.3.224, layer version 1:
	Layer Extensions: count = 0
	Devices: count = 1
		GPU id = 0 (NVIDIA GeForce RTX 3070 Ti)
		Layer-Device Extensions: count = 1

...

```

Make sure everything looks correct before continuing!

# Targetting Vulkan

To generate Halide code for Vulkan, simply add the `vulkan` flag to your target.

For AOT generators add `vulkan` to the target command line option:

```
$ ./lesson_15_generate -g my_first_generator -o . target=host-vulkan
```

For JIT apps use the `HL_JIT_TARGET` environment variable:

```
$ HL_JIT_TARGET=host-vulkan ./tutorial/lesson_01_basics
```

# Useful Environment Variables

`HL_VK_LAYERS=...` will tell Halide to choose a suitable Vulkan instance
that supports the given list of layers. If not set, `VK_INSTANCE_LAYERS=...` 
will be used instead. If neither are present, Halide will use the first 
Vulkan compute device it can find.

`HL_VK_DEVICE_TYPE=...` will tell Halide to choose which type of device
to select for creating the Vulkan instance. Valid options are 'gpu', 
'discrete-gpu', 'integrated-gpu', 'virtual-gpu', or 'cpu'. If not set,
Halide will search for the first 'gpu' like device it can find, or fall back
to the first compute device it can find.

`HL_VK_MIN_BLOCK_SIZE=N` will tell Halide to configure the Vulkan memory
allocator to always request a minimum of N megabytes for a resource block,
which will be used as a pool for suballocations.  Increasing this value
may improve performance while sacrificing the amount of available device 
memory. Default is 32MB.

`HL_VK_MAX_BLOCK_SIZE=N` will tell Halide to configure the Vulkan memory
allocator to never exceed a maximum of N megabytes for a resource block,
which will be used as a pool for suballocations.  Decreasing this value
may free up more memory but may impact performance, and/or restrict 
allocations to be unusably small. Default is 0 ... meaning no limit.

`HL_VK_MAX_BLOCK_COUNT=N` will tell Halide to configure the Vulkan memory
allocator to never exceed a total of N block allocations.  Decreasing this 
value may free up more memory but may impact performance, and/or restrict 
allocations. Default is 0 ... meaning no limit.

`HL_DEBUG_CODEGEN=3` will print out debug info that includees the SPIR-V
code generator used for Vulkan while it is compiling.

`HL_SPIRV_DUMP_FILE=...` specifies a file to dump the binary SPIR-V generated
during compilation. Useful for debugging CodeGen issues. Can be inspected,
validated and disassembled via the SPIR-V tools:

https://github.com/KhronosGroup/SPIRV-Tools


# Known Limitations And Caveats

-   During CodeGen we enable capabilities in the SPIR-V binary for 
    Int8, Int16, Float16, and Float64 based on the Halide IR, assuming
    the device will support these requirements.  We may need limit 
    these if targetting a lower class device.

# Known TODO:

-   Fix as many tests as possible
-   Shared local memory and barriers need work
-   More platform support (e.g. Windows, Android, etc)
-   Better debugging utilities using the Vulkan debug hooks.
-   Allow debug symbols to be stripped from SPIR-V during codegen to reduce
    memory overhead for large kernels.
-   Investigate floating point rounding and precision (v1.3 adds more controls)
-   Investigate memory model usage (are Halide's assumptions correct?)

# Current Status

The following table outlines the state of the correctness tests (as of Sep-30) when 
run with `HL_JIT_TARGET=host-vulkan` (NOTE: some tests may need additional 
modifications to run under Vulkan):

| Totals | *PASS* 🟢 | *FAIL* 🔴 | 
| --     | --        | --        |
|        | 300       | 65        |


| Test Name | Status |
| :--       |    --: |
| correctness_cse_nan | *PASS* 🟢 |
| correctness_simd_op_check_hvx | *PASS* 🟢 |
| correctness_compute_with_in | *PASS* 🟢 |
| correctness_extern_stage | *PASS* 🟢 |
| correctness_pipeline_set_jit_externs_func | *PASS* 🟢 |
| correctness_likely | *PASS* 🟢 |
| correctness_vector_bounds_inference | *PASS* 🟢 |
| correctness_inline_reduction | *PASS* 🟢 |
| correctness_unsafe_promises | *PASS* 🟢 |
| correctness_reorder_rvars | *FAIL* 🔴 | 
| correctness_lossless_cast | *PASS* 🟢 |
| correctness_gpu_reuse_shared_memory | *FAIL* 🔴 | 
| correctness_boundary_conditions | *FAIL* 🔴 | 
| correctness_min_extent | *PASS* 🟢 |
| correctness_gpu_sum_scan | *FAIL* 🔴 | 
| correctness_dynamic_allocation_in_gpu_kernel | *FAIL* 🔴 | 
| correctness_image_of_lists | *PASS* 🟢 |
| correctness_tracing_broadcast | *PASS* 🟢 |
| correctness_scatter | *PASS* 🟢 |
| correctness_stmt_to_html | *PASS* 🟢 |
| correctness_host_alignment | *PASS* 🟢 |
| correctness_custom_allocator | *PASS* 🟢 |
| correctness_issue_3926 | *PASS* 🟢 |
| correctness_compare_vars | *PASS* 🟢 |
| correctness_non_vector_aligned_embeded_buffer | *PASS* 🟢 |
| correctness_realize_larger_than_two_gigs | *PASS* 🟢 |
| correctness_gpu_transpose | *FAIL* 🔴 | 
| correctness_side_effects | *PASS* 🟢 |
| correctness_logical | *FAIL* 🔴 | 
| correctness_func_lifetime_2 | *PASS* 🟢 |
| correctness_device_crop | *FAIL* 🔴 | 
| correctness_print_loop_nest | *PASS* 🟢 |
| correctness_bool_compute_root_vectorize | *FAIL* 🔴 | 
| correctness_extract_concat_bits | *PASS* 🟢 |
| correctness_dead_realization_in_specialization | *PASS* 🟢 |
| correctness_undef | *FAIL* 🔴 | 
| correctness_growing_stack | *PASS* 🟢 |
| correctness_parallel_scatter | *PASS* 🟢 |
| correctness_multi_splits_with_diff_tail_strategies | *PASS* 🟢 |
| correctness_gpu_arg_types | *PASS* 🟢 |
| correctness_cascaded_filters | *FAIL* 🔴 | 
| correctness_trim_no_ops | *FAIL* 🔴 | 
| correctness_float16_t_comparison | *PASS* 🟢 |
| correctness_legal_race_condition | *PASS* 🟢 |
| correctness_explicit_inline_reductions | *PASS* 🟢 |
| correctness_vector_tile | *PASS* 🟢 |
| correctness_skip_stages_memoize | *PASS* 🟢 |
| correctness_intrinsics | *PASS* 🟢 |
| correctness_strict_float | *PASS* 🟢 |
| correctness_bounds_query | *PASS* 🟢 |
| correctness_vector_reductions | *PASS* 🟢 |
| correctness_custom_lowering_| *PASS* 🟢 | | *PASS* 🟢 |
| correctness_gpu_assertion_in_kernel | *PASS* 🟢 |
| correctness_low_bit_depth_noise | *PASS* 🟢 |
| correctness_fuse | *FAIL* 🔴 | 
| correctness_vector_cast | *FAIL* 🔴 | 
| correctness_concat | *PASS* 🟢 |
| correctness_mod | *PASS* 🟢 |
| correctness_parallel_rvar | *PASS* 🟢 |
| correctness_make_struct | *PASS* 🟢 |
| correctness_reduction_predicate_racing | *PASS* 🟢 |
| correctness_bounds_inference_chunk | *PASS* 🟢 |
| correctness_realize_over_shifted_domain | *PASS* 🟢 |
| correctness_compute_at_split_rvar | *PASS* 🟢 |
| correctness_split_fuse_rvar | *PASS* 🟢 |
| correctness_memoize_cloned | *PASS* 🟢 |
| correctness_| *FAIL* 🔴 | _unroll | *PASS* 🟢 |
| correctness_gpu_vectorized_shared_memory | *PASS* 🟢 |
| correctness_bounds_inference_complex | *PASS* 🟢 |
| correctness_widening_reduction | *FAIL* 🔴 | 
| correctness_extern_partial | *PASS* 🟢 |
| correctness_multi_output_pipeline_with_bad_sizes | *PASS* 🟢 |
| correctness_hoist_loop_invariant_if_statements | *PASS* 🟢 |
| correctness_extern_sort | *FAIL* 🔴 | 
| correctness_multiple_outputs_extern | *PASS* 🟢 |
| correctness_tracing_bounds | *PASS* 🟢 |
| correctness_gpu_object_lifetime_1 | *PASS* 🟢 |
| correctness_nested_tail_strategies | *PASS* 🟢 |
| correctness_parallel_reductions | *PASS* 🟢 |
| correctness_custom_error_reporter | *PASS* 🟢 |
| correctness_many_dimensions | *PASS* 🟢 |
| correctness_predicated_store_load | *PASS* 🟢 |
| correctness_random | *PASS* 🟢 |
| correctness_partition_loops_bug | *PASS* 🟢 |
| correctness_stencil_chain_in_update_definitions | *PASS* 🟢 |
| correctness_inverse | *PASS* 🟢 |
| correctness_skip_stages | *PASS* 🟢 |
| correctness_cuda_8_bit_dot_product | *PASS* 🟢 |
| correctness_gpu_vectorize | *FAIL* 🔴 | 
| correctness_gpu_object_lifetime_3 | *FAIL* 🔴 | 
| correctness_histogram | *PASS* 🟢 |
| correctness_shared_self_references | *PASS* 🟢 |
| correctness_gpu_mixed_shared_mem_types | *FAIL* 🔴 | 
| correctness_custom_cuda_context | *PASS* 🟢 |
| correctness_implicit_args_tests | *PASS* 🟢 |
| correctness_compile_to_lowered_stmt | *PASS* 🟢 |
| correctness_bounds_of_func | *PASS* 🟢 |
| correctness_interleave_rgb | *FAIL* 🔴 | 
| correctness_multi_gpu_gpu_multi_device | *PASS* 🟢 |
| correctness_lambda | *PASS* 🟢 |
| correctness_interval | *PASS* 🟢 |
| correctness_unused_func | *PASS* 🟢 |
| correctness_fuzz_float_stores | *PASS* 🟢 |
| correctness_newtons_method | *FAIL* 🔴 | 
| correctness_compile_to_bitcode | *PASS* 🟢 |
| correctness_lazy_convolution | *PASS* 🟢 |
| correctness_image_wrapper | *PASS* 🟢 |
| correctness_reduction_chain | *PASS* 🟢 |
| correctness_storage_folding | *PASS* 🟢 |
| correctness_reorder_storage | *PASS* 🟢 |
| correctness_bit_counting | *PASS* 🟢 |
| correctness_tiled_matmul | *PASS* 🟢 |
| correctness_async_device_copy | *FAIL* 🔴 | 
| correctness_lots_of_dimensions | *PASS* 🟢 |
| correctness_interleave | *PASS* 🟢 |
| correctness_dynamic_reduction_bounds | *PASS* 🟢 |
| correctness_atomic_tuples | *PASS* 🟢 |
| correctness_named_updates | *PASS* 🟢 |
| correctness_unroll_dynamic_loop | *PASS* 🟢 |
| correctness_buffer_t | *PASS* 🟢 |
| correctness_hello_gpu | *PASS* 🟢 |
| correctness_gpu_object_lifetime_2 | *FAIL* 🔴 | 
| correctness_update_chunk | *PASS* 🟢 |
| correctness_autodiff | *PASS* 🟢 |
| correctness_extern_consumer | *PASS* 🟢 |
| correctness_func_wrapper | *PASS* 🟢 |
| correctness_bounds_of_multiply | *PASS* 🟢 |
| correctness_gpu_store_in_register_with_no_lanes_loop | *FAIL* 🔴 | 
| correctness_gpu_condition_lifting | *PASS* 🟢 |
| correctness_extern_consumer_tiled | *PASS* 🟢 |
| correctness_float16_t_neon_op_check | *PASS* 🟢 |
| correctness_split_by_non_factor | *PASS* 🟢 |
| correctness_parallel_fork | *PASS* 🟢 |
| correctness_hexagon_scatter | *PASS* 🟢 |
| correctness_partition_loops | *PASS* 🟢 |
| correctness_process_some_tiles | *PASS* 🟢 |
| correctness_parameter_constraints | *PASS* 🟢 |
| correctness_callable | *PASS* 🟢 |
| correctness_bounds_inference | *FAIL* 🔴 | 
| correctness_indexing_access_undef | *PASS* 🟢 |
| correctness_partial_realization | *PASS* 🟢 |
| correctness_gpu_mixed_dimensionality | *FAIL* 🔴 | 
| correctness_uninitialized_read | *PASS* 🟢 |
| correctness_unsafe_dedup_lets | *PASS* 🟢 |
| correctness_output_larger_than_two_gigs | *PASS* 🟢 |
| correctness_obscure_image_references | *PASS* 🟢 |
| correctness_chunk | *FAIL* 🔴 | 
| correctness_vectorized_load_from_vectorized_allocation | *PASS* 🟢 |
| correctness_load_library | *PASS* 🟢 |
| correctness_compute_inside_guard | *PASS* 🟢 |
| correctness_multi_| *PASS* 🟢 |_reduction | *PASS* 🟢 |
| correctness_lerp | *PASS* 🟢 |
| correctness_realize_condition_depends_on_tuple | *PASS* 🟢 |
| correctness_vectorized_initialization | *PASS* 🟢 |
| correctness_loop_level_generator_param | *PASS* 🟢 |
| correctness_two_vector_args | *PASS* 🟢 |
| correctness_argmax | *FAIL* 🔴 | 
| correctness_custom_auto_scheduler | *PASS* 🟢 |
| correctness_shadowed_bound | *PASS* 🟢 |
| correctness_inlined_generator | *PASS* 🟢 |
| correctness_math | *FAIL* 🔴 | 
| correctness_gpu_different_blocks_threads_dimensions | *PASS* 🟢 |
| correctness_extern_stage_on_device | *FAIL* 🔴 | 
| correctness_bound | *PASS* 🟢 |
| correctness_popc_clz_ctz_bounds | *PASS* 🟢 |
| correctness_bounds | *PASS* 🟢 |
| correctness_prefetch | *PASS* 🟢 |
| correctness_force_onto_stack | *PASS* 🟢 |
| correctness_input_image_bounds_check | *PASS* 🟢 |
| correctness_sort_exprs | *PASS* 🟢 |
| correctness_let_in_rdom_bound | *PASS* 🟢 |
| correctness_func_lifetime | *PASS* 🟢 |
| correctness_compute_outermost | *PASS* 🟢 |
| correctness_histogram_equalize | *PASS* 🟢 |
| correctness_func_clone | *PASS* 🟢 |
| correctness_tracing_stack | *PASS* 🟢 |
| correctness_simplify | *PASS* 🟢 |
| correctness_gameoflife | *PASS* 🟢 |
| correctness_thread_safety | *PASS* 🟢 |
| correctness_fuse_gpu_threads | *PASS* 🟢 |
| correctness_split_reuse_inner_name_bug | *PASS* 🟢 |
| correctness_gpu_jit_explicit_copy_to_device | *FAIL* 🔴 | 
| correctness_tuple_select | *PASS* 🟢 |
| correctness_device_buffer_copy | *FAIL* 🔴 | 
| correctness_pseudostack_shares_slots | *PASS* 🟢 |
| correctness_lots_of_loop_invariants | *PASS* 🟢 |
| correctness_fuzz_simplify | *PASS* 🟢 |
| correctness_div_round_to_zero | *PASS* 🟢 |
| correctness_rfactor | *PASS* 🟢 |
| correctness_custom_jit_context | *PASS* 🟢 |
| correctness_round | *PASS* 🟢 |
| correctness_device_slice | *FAIL* 🔴 | 
| correctness_iterate_over_circle | *PASS* 🟢 |
| correctness_vector_print_bug | *PASS* 🟢 |
| correctness_mux | *PASS* 🟢 |
| correctness_vectorize_varying_allocation_size | *PASS* 🟢 |
| correctness_parallel_nested_1 | *PASS* 🟢 |
| correctness_compile_to_multitarget | *PASS* 🟢 |
| correctness_bounds_inference_outer_split | *PASS* 🟢 |
| correctness_leak_device_memory | *FAIL* 🔴 | 
| correctness_reduction_schedule | *PASS* 🟢 |
| correctness_many_small_extern_stages | *PASS* 🟢 |
| correctness_parallel_alloc | *PASS* 🟢 |
| correctness_multiple_outputs | *FAIL* 🔴 | 
| correctness_vectorize_nested | *PASS* 🟢 |
| correctness_bad_likely | *PASS* 🟢 |
| correctness_sliding_reduction | *PASS* 🟢 |
| correctness_bounds_of_split | *PASS* 🟢 |
| correctness_erf | *PASS* 🟢 |
| correctness_float16_t_image_type | *PASS* 🟢 |
| correctness_gpu_non_monotonic_shared_mem_size | *FAIL* 🔴 | 
| correctness_extern_reorder_storage | *PASS* 🟢 |
| correctness_gather | *PASS* 🟢 |
| correctness_gpu_many_kernels | *PASS* 🟢 |
| correctness_early_out | *PASS* 🟢 |
| correctness_strict_float_bounds | *PASS* 🟢 |
| correctness_bounds_of_abs | *PASS* 🟢 |
| correctness_tuple_vector_reduce | *PASS* 🟢 |
| correctness_debug_to_file_reorder | *FAIL* 🔴 | 
| correctness_vectorized_reduction_bug | *PASS* 🟢 |
| correctness_input_larger_than_two_gigs | *PASS* 🟢 |
| correctness_computed_index | *PASS* 🟢 |
| correctness_reduction_non_rectangular | *FAIL* 🔴 | 
| correctness_left_shift_negative | *PASS* 🟢 |
| correctness_set_custom_trace | *PASS* 🟢 |
| correctness_vectorized_gpu_allocation | *FAIL* 🔴 | 
| correctness_split_store_compute | *PASS* 🟢 |
| correctness_c_function | *PASS* 🟢 |
| correctness_specialize | *PASS* 🟢 |
| correctness_nested_shiftinwards | *PASS* 🟢 |
| correctness_assertion_failure_in_parallel_for | *PASS* 🟢 |
| correctness_plain_c_includes | *PASS* 🟢 |
| correctness_stream_compaction | *PASS* 🟢 |
| correctness_async | *PASS* 🟢 |
| correctness_atomics | *PASS* 🟢 |
| correctness_multi| *PASS* 🟢 |_constraints | *PASS* 🟢 |
| correctness_target | *PASS* 🟢 |
| correctness_tuple_reduction | *FAIL* 🔴 | 
| correctness_dilate3x3 | *FAIL* 🔴 | 
| correctness_image_io | *PASS* 🟢 |
| correctness_gpu_param_allocation | *FAIL* 🔴 | 
| correctness_reschedule | *PASS* 🟢 |
| correctness_isnan | *FAIL* 🔴 | 
| correctness_halide_buffer | *PASS* 🟢 |
| correctness_bounds_of_cast | *PASS* 🟢 |
| correctness_handle | *PASS* 🟢 |
| correctness_param | *PASS* 🟢 |
| correctness_saturating_casts | *PASS* 🟢 |
| correctness_extern_producer | *FAIL* 🔴 | 
| correctness_shift_by_unsigned_negated | *PASS* 🟢 |
| correctness_circular_reference_leak | *PASS* 🟢 |
| correctness_specialize_to_gpu | *FAIL* 🔴 | 
| correctness_device_copy_at_inner_loop | *FAIL* 🔴 | 
| correctness_fit_function | *PASS* 🟢 |
| correctness_compute_at_reordered_update_stage | *PASS* 🟢 |
| correctness_non_nesting_extern_bounds_query | *PASS* 🟢 |
| correctness_bitwise_ops | *PASS* 🟢 |
| correctness_gpu_data_flows | *FAIL* 🔴 | 
| correctness_cast | *PASS* 🟢 |
| correctness_stack_allocations | *PASS* 🟢 |
| correctness_sliding_backwards | *PASS* 🟢 |
| correctness_float16_t | *PASS* 🟢 |
| correctness_simd_op_check | *PASS* 🟢 |
| correctness_typed_func | *PASS* 🟢 |
| correctness_tuple_partial_update | *PASS* 🟢 |
| correctness_heap_cleanup | *PASS* 🟢 |
| correctness_implicit_args | *PASS* 🟢 |
| correctness_deferred_loop_level | *PASS* 🟢 |
| correctness_interleave_x | *PASS* 🟢 |
| correctness_fuzz_bounds | *PASS* 🟢 |
| correctness_strided_load | *PASS* 🟢 |
| correctness_bound_storage | *PASS* 🟢 |
| correctness_gpu_cpu_simultaneous_read | *FAIL* 🔴 | 
| correctness_fast_trigonometric | *PASS* 🟢 |
| correctness_compute_with | *FAIL* 🔴 | 
| correctness_gpu_allocation_cache | *FAIL* 🔴 | 
| correctness_compile_to | *PASS* 🟢 |
| correctness_extern_output_expansion | *PASS* 🟢 |
| correctness_gpu_texture | *PASS* 🟢 |
| correctness_many_updates | *PASS* 🟢 |
| correctness_memoize | *PASS* 🟢 |
| correctness_gpu_multi_kernel | *FAIL* 🔴 | 
| correctness_extern_error | *PASS* 🟢 |
| correctness_partition_max_filter | *PASS* 🟢 |
| correctness_bound_small_allocations | *PASS* 🟢 |
| correctness_median3x3 | *FAIL* 🔴 | 
| correctness_reuse_stack_alloc | *PASS* 🟢 |
| correctness_debug_to_file | *FAIL* 🔴 | 
| correctness_embed_bitcode | *PASS* 🟢 |
| correctness_gpu_large_alloc | *FAIL* 🔴 | 
| correctness_pytorch | *PASS* 🟢 |
| correctness_in_place | *FAIL* 🔴 | 
| correctness_exception | *PASS* 🟢 |
| correctness_python_extension_gen | *PASS* 🟢 |
| correctness_cross_compilation | *PASS* 🟢 |
| correctness_extern_bounds_inference | *PASS* 🟢 |
| correctness_bounds_of_monotonic_math | *PASS* 🟢 |
| correctness_loop_invariant_extern_calls | *PASS* 🟢 |
| correctness_skip_stages_external_array_functions | *PASS* 🟢 |
| correctness_chunk_sharing | *PASS* 🟢 |
| correctness_multi_way_select | *FAIL* 🔴 | 
| correctness_async_copy_chain | *FAIL* 🔴 | 
| correctness_gpu_give_input_buffers_device_allocations | *FAIL* 🔴 | 
| correctness_oddly_sized_output | *PASS* 🟢 |
| correctness_fuzz_cse | *PASS* 🟢 |
| correctness_half_native_interleave | *PASS* 🟢 |
| correctness_introspection | *PASS* 🟢 |
| correctness_callable_generator | *PASS* 🟢 |
| correctness_fused_where_inner_extent_is_zero | *PASS* 🟢 |
| correctness_tuple_update_ops | *PASS* 🟢 |
| correctness_constraints | *PASS* 🟢 |
| correctness_multiple_scatter | *PASS* 🟢 |
| correctness_unrolled_reduction | *PASS* 🟢 |
| correctness_tracing | *PASS* 🟢 |
| correctness_simplified_away_embedded_image | *PASS* 🟢 |
| correctness_mul_div_mod | *FAIL* 🔴 | 
| correctness_infer_arguments | *PASS* 🟢 |
| correctness_convolution | *FAIL* 🔴 | 
| correctness_truncated_pyramid | *PASS* 🟢 |
| correctness_for_each_element | *PASS* 🟢 |
| correctness_store_in | *PASS* 🟢 |
| correctness_transitive_bounds | *PASS* 🟢 |
| correctness_vectorize_guard_with_if | *PASS* 🟢 |
| correctness_widening_lerp | *PASS* 🟢 |
| correctness_cast_handle | *PASS* 🟢 |
| correctness_tuple_undef | *PASS* 🟢 |
| correctness_partial_application | *PASS* 🟢 |
| correctness_vectorize_mixed_widths | *PASS* 🟢 |
| correctness_print | *PASS* 🟢 |
| correctness_fibonacci | *PASS* 🟢 |
| correctness_parallel_nested | *PASS* 🟢 |
| correctness_sliding_window | *PASS* 🟢 |
| correctness_integer_powers | *PASS* 🟢 |
| correctness_unique_func_image | *PASS* 🟢 |
| correctness_constant_type | *PASS* 🟢 |
| correctness_shifted_image | *PASS* 🟢 |
| correctness_vector_extern | *PASS* 🟢 |
| correctness_compute_with_inlined | *PASS* 🟢 |
| correctness_param_map | *PASS* 🟢 |
| correctness_float16_t_constants | *PASS* 🟢 |
| correctness_callable_typed | *PASS* 🟢 |
| correctness_unroll_huge_mux | *PASS* 🟢 |
| correctness_parallel | *PASS* 🟢 |
| correctness_code_explosion | *PASS* 🟢 |
| correctness_gpu_dynamic_shared | *FAIL* 🔴 | 
| correctness_div_by_zero | *PASS* 🟢 |
| correctness_convolution_multiple_kernels | *FAIL* 🔴 | 
| correctness_deinterleave4 | *PASS* 🟢 |
| correctness_align_bounds | *PASS* 🟢 |
| correctness_gpu_bounds_inference_failure | *PASS* 🟢 |
| correctness_interpreter | *FAIL* 🔴 | 
| correctness_parallel_gpu_nested | *PASS* 🟢 |
| correctness_gpu_thread_barrier | *FAIL* 🔴 | 
| correctness_debug_to_file_multiple_outputs | *PASS* 🟢 |
| correctness_gpu_free_sync | *PASS* 🟢 |
| correctness_out_constraint | *PASS* 🟢 |
| correctness_gpu_specialize | *PASS* 🟢| 
| correctness_register_shuffle | *PASS* 🟢 |
| correctness_constant_expr | *PASS* 🟢 |
| correctness_out_of_memory | *PASS* 🟢 |
| correctness_gpu_non_contiguous_copy | *PASS* 🟢 |
| correctness_sliding_over_guard_with_if | *PASS* 🟢 |
| correctness_vector_math | *PASS* 🟢 |
| correctness_require | *PASS* 🟢 |
| correctness_callable_errors | *PASS* 🟢 |

