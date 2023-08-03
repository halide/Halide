add_library(Halide_Plugin INTERFACE)
add_library(Halide::Plugin ALIAS Halide_Plugin)
target_include_directories(Halide_Plugin INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(Halide_Plugin INTERFACE Halide::Halide)

add_library(ASLog STATIC ASLog.cpp)
target_include_directories(ASLog PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
set_property(TARGET ASLog PROPERTY POSITION_INDEPENDENT_CODE YES)

# Sigh, header-only libraries shouldn't be special
add_library(ParamParser INTERFACE)
target_include_directories(ParamParser INTERFACE
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

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
    set_tests_properties(test_perfect_hash_map PROPERTIES LABELS "autoschedulers_cpu;auto_schedule")
endif()
