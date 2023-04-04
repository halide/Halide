tests(GROUPS fuzz
      SOURCES
      simplify.cpp
)


target_link_libraries(fuzz_simplify PRIVATE Halide::Halide)

# Allow OSS-fuzz to manage flags directly
if (DEFINED ENV{LIB_FUZZING_ENGINE})
  target_link_options(fuzz_simplify PRIVATE "${LIB_FUZZING_ENGINE}")
else ()
  # By default just build with address sanitizers/libfuzzer for local testing
  target_compile_options(fuzz_simplify PRIVATE -fsanitize=fuzzer-no-link,address)
  target_link_options(fuzz_simplify PRIVATE -fsanitize=fuzzer)
endif ()

