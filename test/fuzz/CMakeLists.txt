tests(GROUPS fuzz
      SOURCES
      simplify.cpp
)


# By default we are going to use the libfuzzer engine. However if 
# LIB_FUZZING_ENGINE is declared you can override the fuzzing engine to one of;
# - Centipede
# - Hongfuzz
# - AFL++
# - etc.
set(LIB_FUZZING_ENGINE "$ENV{LIB_FUZZING_ENGINE}"
    CACHE STRING "Compiler flags necessary to link the fuzzing engine of choice e.g. libfuzzer, afl etc.")

target_link_libraries(fuzz_simplify PRIVATE Halide::Halide)

# Allow OSS-fuzz to manage flags directly
if (LIB_FUZZING_ENGINE)
  target_link_libraries(fuzz_simplify PRIVATE "${LIB_FUZZING_ENGINE}")
else ()
  # By default just build with address-sanitizers/libfuzzer for local testing
  target_compile_options(fuzz_simplify PRIVATE -fsanitize=fuzzer-no-link,address)
  target_link_options(fuzz_simplify PRIVATE -fsanitize=fuzzer,address)
endif ()

