include(HalideTestHelpers)

# Internal tests are a special case.
# HalideTestHelpers depends on this test being present.
add_executable(_test_internal internal.cpp)
target_link_libraries(_test_internal PRIVATE Halide::Test)
target_include_directories(_test_internal PRIVATE "${Halide_SOURCE_DIR}/src")
target_precompile_headers(_test_internal PRIVATE <Halide.h>)

add_halide_test(_test_internal GROUPS internal)

option(WITH_TEST_AUTO_SCHEDULE "Build auto_schedule tests" ON)
if (WITH_TEST_AUTO_SCHEDULE)
    add_subdirectory(auto_schedule)
endif ()

option(WITH_TEST_CORRECTNESS "Build correctness tests" ON)
if (WITH_TEST_CORRECTNESS)
    add_subdirectory(correctness)
endif ()

option(WITH_TEST_ERROR "Build error tests" ON)
if (WITH_TEST_ERROR)
    add_subdirectory(error)
endif ()

option(WITH_TEST_WARNING "Build warning tests" ON)
if (WITH_TEST_WARNING)
    add_subdirectory(warning)
endif ()

option(WITH_TEST_PERFORMANCE "Build performance tests" ON)
if (WITH_TEST_PERFORMANCE)
    add_subdirectory(performance)
endif ()

option(WITH_TEST_GENERATOR "Build generator tests" ON)
if (WITH_TEST_GENERATOR)
    add_subdirectory(generator)
endif ()

# FIXME: failing_with_issue is dead code :)
