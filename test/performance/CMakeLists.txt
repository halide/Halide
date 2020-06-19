tests(GROUPS performance
      SOURCES
      async_gpu.cpp
      block_transpose.cpp
      boundary_conditions.cpp
      clamped_vector_load.cpp
      const_division.cpp
      fan_in.cpp
      fast_inverse.cpp
      fast_pow.cpp
      fast_sine_cosine.cpp
      gpu_half_throughput.cpp
      inner_loop_parallel.cpp
      jit_stress.cpp
      lots_of_inputs.cpp
      lots_of_small_allocations.cpp
      matrix_multiplication.cpp
      memcpy.cpp
      memory_profiler.cpp
      packed_planar_fusion.cpp
      parallel_performance.cpp
      profiler.cpp
      realize_overhead.cpp
      rfactor.cpp
      rgb_interleaved.cpp
      sort.cpp
      thread_safe_jit.cpp
      vectorize.cpp
      wrap.cpp
      )

# Make sure that performance tests do not run in parallel with other tests,
# since doing so might make them flaky.
set_tests_properties(${TEST_NAMES} PROPERTIES RUN_SERIAL TRUE)

# This test needs rdynamic or equivalent
set_target_properties(performance_fast_pow PROPERTIES ENABLE_EXPORTS TRUE)
