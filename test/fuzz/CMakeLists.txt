if (NOT LIB_FUZZING_ENGINE)
    set(LIB_FUZZING_ENGINE "$ENV{LIB_FUZZING_ENGINE}")
endif ()

set(LIB_FUZZING_ENGINE "${LIB_FUZZING_ENGINE}"
    CACHE STRING "Extra link libraries arguments for fuzz tests.")

tests(GROUPS fuzz
      SOURCES
      bounds.cpp
      cse.cpp
      lossless_cast.cpp
      simplify.cpp
      widening_lerp.cpp
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

# Check whether the configured toolchain can compile a libfuzzer test harness.
# If this fails, the user likely needs to set up their environment to use a
# compatible fuzzing engine (e.g. by setting up a toolchain file).
check_cxx_source_compiles([[
#include <cstdint>
#include <cstddef>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, std::size_t Size) {
  return 0;
}
]] HAVE_LIBFUZZER_FLAGS)

add_library(Halide_fuzz OBJECT)
add_library(Halide::fuzz ALIAS Halide_fuzz)

target_sources(Halide_fuzz PRIVATE IRGraphCXXPrinter.cpp)
target_link_libraries(Halide_fuzz PRIVATE Halide::Test)

if (NOT HAVE_LIBFUZZER_FLAGS)
    if (LIB_FUZZING_ENGINE)
        message(FATAL_ERROR "Cannot set LIB_FUZZING_ENGINE when not building with -fsanitize=fuzzer or a compatible fuzzing engine.")
    endif ()
    target_sources(Halide_fuzz PRIVATE halide_fuzz_main.cpp halide_fuzz_main.h)
    target_compile_definitions(Halide_fuzz PUBLIC HALIDE_FUZZER_BACKEND=HALIDE_FUZZER_BACKEND_STDLIB)
else ()
    target_link_libraries(Halide_fuzz PUBLIC ${LIB_FUZZING_ENGINE})
    target_compile_definitions(Halide_fuzz PUBLIC HALIDE_FUZZER_BACKEND=HALIDE_FUZZER_BACKEND_LIBFUZZER)
endif ()


foreach (fuzzer IN LISTS TEST_NAMES)
    target_link_libraries("${fuzzer}" PRIVATE Halide::fuzz)
endforeach ()
