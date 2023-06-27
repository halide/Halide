tests(GROUPS roundtrip
      SOURCES
      roundtrip.cpp
      )

set_tests_properties(${TEST_NAMES} PROPERTIES RUN_SERIAL TRUE)

