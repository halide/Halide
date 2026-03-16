include(HalideTestHelpers)
include(CheckCXXCompilerFlag)

# Internal tests are a special case.
# HalideTestHelpers depends on this test being present.
add_executable(_test_internal internal.cpp)
target_link_libraries(_test_internal PRIVATE Halide::Test Halide::TerminateHandler)
target_include_directories(_test_internal PRIVATE "${Halide_SOURCE_DIR}/src")
target_precompile_headers(_test_internal PRIVATE <Halide.h>)
if (Halide_CCACHE_BUILD)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(
            _test_internal PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-Xclang -fno-pch-timestamp>"
        )
    endif ()
endif ()

add_halide_test(_test_internal GROUPS internal)

Halide_feature(WITH_TEST_AUTO_SCHEDULE "Build autoscheduler tests" AUTO
               DEPENDS WITH_AUTOSCHEDULERS)
if (WITH_TEST_AUTO_SCHEDULE)
    add_subdirectory(autoschedulers)
endif ()

Halide_feature(WITH_TEST_CORRECTNESS "Build correctness tests" ON)
if (WITH_TEST_CORRECTNESS)
    add_subdirectory(correctness)
endif ()

Halide_feature(WITH_TEST_ERROR "Build error tests" ON)
if (WITH_TEST_ERROR)
    add_subdirectory(error)
endif ()

Halide_feature(WITH_TEST_WARNING "Build warning tests" ON)
if (WITH_TEST_WARNING)
    add_subdirectory(warning)
endif ()

Halide_feature(WITH_TEST_PERFORMANCE "Build performance tests" ON)
if (WITH_TEST_PERFORMANCE)
    add_subdirectory(performance)
endif ()

Halide_feature(WITH_TEST_GENERATOR "Build generator tests" ON)
if (WITH_TEST_GENERATOR)
    add_subdirectory(generator)
endif ()

# FIXME: Disable the runtime tests for MSVC until we have a MS compatible header.
#
# The runtime tests include src/runtime/runtime_internal.h which was written
# to only support clang (GCC's front end is close enough it works fine as well).
# We originally setup the tests to compile with clang (in the same way as the actual
# runtime bitcode files), but that wasn't very clean and didn't integrate well with
# the other tests, so we switched to just using the native system compiler.
# Sadly MSVC isn't compatible with the current runtime_internal.h which would need
# some platform specific ifdefs for attributes and types that are causing compile
# errors.
#
Halide_feature(WITH_TEST_RUNTIME "Build runtime tests" AUTO
               DEPENDS NOT MSVC)
if (WITH_TEST_RUNTIME)
    add_subdirectory(runtime)
endif ()

# FIXME: failing_with_issue is dead code :)

# Check that the configured toolchain can compile a libfuzzer test harness. If
# this fails, the user likely needs to set up their environment to use a
# compatible fuzzing engine (e.g. by setting up a toolchain file).
check_cxx_source_compiles([[
#include <cstdint>
#include <cstddef>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, std::size_t Size) {
  return 0;
}
]] HAVE_LIBFUZZER_FLAGS)

Halide_feature(WITH_TEST_FUZZ "Build fuzz tests" AUTO
               DEPENDS HAVE_LIBFUZZER_FLAGS)
if (WITH_TEST_FUZZ)
    add_subdirectory(fuzz)
endif ()
