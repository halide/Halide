add_library(Halide_Plugin INTERFACE)
add_library(Halide::Plugin ALIAS Halide_Plugin)
target_sources(
    Halide_Plugin
    INTERFACE
    FILE_SET HEADERS
    FILES
    Errors.h
    HalidePlugin.h
    ParamParser.h
    cmdline.h
    PerfectHashMap.h
)
target_link_libraries(Halide_Plugin INTERFACE Halide::Halide Halide::ASLog)

add_library(Halide_ASLog STATIC)
add_library(Halide::ASLog ALIAS Halide_ASLog)
target_sources(
    Halide_ASLog
    PRIVATE ASLog.cpp
    PUBLIC
    FILE_SET HEADERS
    FILES ASLog.h
)
set_property(TARGET Halide_ASLog PROPERTY POSITION_INDEPENDENT_CODE YES)

if (WITH_UTILS)
    add_executable(featurization_to_sample featurization_to_sample.cpp)

    add_executable(get_host_target get_host_target.cpp)
    target_link_libraries(get_host_target PRIVATE Halide::Halide)
endif ()


# =================================================================
# Tests for private/internal functionality of Adams2019 (vs for public functionality,
# which is handled in tests/autoschedulers/Adams2019)

if (WITH_TESTS)
    add_executable(test_perfect_hash_map test_perfect_hash_map.cpp)
    add_test(NAME test_perfect_hash_map COMMAND test_perfect_hash_map)
    set_tests_properties(test_perfect_hash_map PROPERTIES LABELS "autoschedulers_cpu")
endif()
