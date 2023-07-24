tests(GROUPS serialization
      SOURCES
      ../correctness/align_bounds.cpp
)

set_property(TEST serialization_align_bounds PROPERTY ENVIRONMENT "HL_TEST_SERIALIZATION=1")
