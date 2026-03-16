tests(GROUPS fuzz
      SOURCES
      bounds.cpp
      cse.cpp
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

# Allow OSS-fuzz to manage flags directly
set(LIB_FUZZING_ENGINE "$ENV{LIB_FUZZING_ENGINE}"
    CACHE STRING "Extra link libraries arguments for fuzz tests.")

foreach (fuzzer IN LISTS TEST_NAMES)
    target_link_libraries("${fuzzer}" PRIVATE ${LIB_FUZZING_ENGINE})
endforeach ()
