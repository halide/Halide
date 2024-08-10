if (Halide_ANY_SANITIZERS_ENABLED)
    # All sanitizers impact performance, so don't even bother with this test suite
    message(STATUS "Skipping all performance testing because at least one Sanitizer is enabled.")
    return()
endif()

tests(GROUPS performance
      SOURCES
      async_gpu.cpp
      blend_tail_strategies.cpp
      block_transpose.cpp
      boundary_conditions.cpp
      clamped_vector_load.cpp
      const_division.cpp
      fast_arctan.cpp
      fast_inverse.cpp
      fast_pow.cpp
      fast_sine_cosine.cpp
      gpu_half_throughput.cpp
      jit_stress.cpp
      lots_of_inputs.cpp
      memcpy.cpp
      nested_vectorization_gemm.cpp
      packed_planar_fusion.cpp
      realize_overhead.cpp
      rgb_interleaved.cpp
      tiled_matmul.cpp
      vectorize.cpp
      wrap.cpp
      )

tests(GROUPS performance multithreaded
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
      stack_vs_heap.cpp
      thread_safe_jit_callable.cpp
      )

# Make sure that performance tests do not run in parallel with other tests,
# since doing so might make them flaky.
set_tests_properties(${TEST_NAMES} PROPERTIES RUN_SERIAL TRUE)

# This test needs rdynamic or equivalent
set_target_properties(performance_fast_pow PROPERTIES ENABLE_EXPORTS TRUE)
