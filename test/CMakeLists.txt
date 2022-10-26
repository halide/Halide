include(HalideTestHelpers)

# Internal tests are a special case.
# HalideTestHelpers depends on this test being present.
add_executable(_test_internal internal.cpp)
target_link_libraries(_test_internal PRIVATE Halide::Test)
target_include_directories(_test_internal PRIVATE "${Halide_SOURCE_DIR}/src")
target_precompile_headers(_test_internal PRIVATE <Halide.h>)

add_halide_test(_test_internal GROUPS internal)

option(WITH_TEST_AUTO_SCHEDULE "Build autoscheduler tests" ON)
if (WITH_TEST_AUTO_SCHEDULE)
    add_subdirectory(autoschedulers)
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
cmake_dependent_option(WITH_TEST_RUNTIME "Build runtime tests" ON
                       "NOT MSVC" OFF)

if (WITH_TEST_RUNTIME)
    message(STATUS "Building internal runtime tests enabled")
    add_subdirectory(runtime)
else ()
    message(STATUS "Building internal runtime tests disabled")
endif ()

# FIXME: failing_with_issue is dead code :)
