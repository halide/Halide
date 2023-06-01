tests(GROUPS fuzz
      SOURCES
      bounds.cpp
      cse.cpp
      simplify.cpp
      # By default, the libfuzzer harness runs with a timeout of 1200 seconds.
      # Let's dial that back:
      # - Do 1000 fuzz runs for each test.
      # - no single input should take more than 2 minutes
      # - total time should take no more than 5 minutes
      ARGS
      -runs=1000
      -timeout=120
      -max_total_time=300
      # These tests don't print "Success!" -- just succeed or fail based on the exit code
      USE_EXIT_CODE_ONLY
)


# By default we are going to use the libfuzzer engine. However if 
# LIB_FUZZING_ENGINE is declared you can override the fuzzing engine to one of;
# - Centipede
# - Hongfuzz
# - AFL++
# - etc.
set(LIB_FUZZING_ENGINE "$ENV{LIB_FUZZING_ENGINE}"
    CACHE STRING "Compiler flags necessary to link the fuzzing engine of choice e.g. libfuzzer, afl etc.")

foreach(fuzzer "fuzz_bounds" "fuzz_cse" "fuzz_simplify")
  target_link_libraries(${fuzzer} PRIVATE Halide::Halide)

  # Allow OSS-fuzz to manage flags directly
  if (LIB_FUZZING_ENGINE)
    target_link_libraries(${fuzzer} PRIVATE "${LIB_FUZZING_ENGINE}")
  else ()
    # By default just build with address-sanitizers/libfuzzer for local testing
    target_compile_options(${fuzzer} PRIVATE -fsanitize=fuzzer-no-link)
    target_link_options(${fuzzer} PRIVATE -fsanitize=fuzzer)
  endif ()
endforeach()

