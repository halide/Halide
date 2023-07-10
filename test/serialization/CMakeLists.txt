tests(GROUPS serialization
      SOURCES
      single_func_pipe.cpp
      multiple_func_pipe.cpp
      )

set_tests_properties(${TEST_NAMES} PROPERTIES RUN_SERIAL TRUE)

