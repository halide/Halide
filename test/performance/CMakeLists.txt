if (Halide_ANY_SANITIZERS_ENABLED)
    # All sanitizers impact performance, so don't even bother with this test suite
    message(STATUS "Skipping all performance testing because at least one Sanitizer is enabled.")
    return()
endif ()

tests(
    GROUPS performance target_from_environment calls_llvm
    SOURCES
    # keep-sorted start
    blend_tail_strategies.cpp
    block_transpose.cpp
    clamped_vector_load.cpp
    const_division.cpp
    fast_inverse.cpp
    fast_pow.cpp
    fast_sine_cosine.cpp
    interleave.cpp
    irmatch_throughput.cpp
    jit_stress.cpp
    lots_of_inputs.cpp
    memcpy.cpp
    nested_vectorization_gemm.cpp
    packed_planar_fusion.cpp
    realize_overhead.cpp
    rgb_interleaved.cpp
    tiled_matmul.cpp
    tiled_matmul_arm_neon.cpp
    vectorize.cpp
    # keep-sorted end
)

tests(
    GROUPS performance gpu target_from_environment calls_llvm
    SOURCES boundary_conditions.cpp device_copy.cpp gpu_half_throughput.cpp wrap.cpp
)

tests(
    GROUPS performance multithreaded target_from_environment calls_llvm
    SOURCES
    fan_in.cpp
    inner_loop_parallel.cpp
    lots_of_small_allocations.cpp
    matrix_multiplication.cpp
    memory_profiler.cpp
    parallel_performance.cpp
    parallel_scenarios.cpp
    profiler.cpp
    rfactor.cpp
    sort.cpp
    split_storage.cpp
    stack_vs_heap.cpp
    thread_safe_jit_callable.cpp
)

tests(
    GROUPS performance gpu multithreaded target_from_environment calls_llvm
    SOURCES async_gpu.cpp split_storage_bank_conflict.cpp
)

# This test needs rdynamic or equivalent
set_target_properties(performance_fast_pow PROPERTIES ENABLE_EXPORTS TRUE)
