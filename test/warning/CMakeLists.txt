tests(GROUPS warning
      SOURCES
      hidden_pure_definition.cpp
      require_const_false.cpp
      sliding_vectors.cpp
      unscheduled_update_def.cpp
      emulated_float16.cpp
      )

# Don't look for "Success!" in warning tests, look for "Warning:" instead.
set_tests_properties(${TEST_NAMES} PROPERTIES PASS_REGULAR_EXPRESSION "Warning:")
