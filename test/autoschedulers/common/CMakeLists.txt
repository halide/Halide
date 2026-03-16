# test_perfect_hash_map (moved from src/autoschedulers/common)

add_executable(test_perfect_hash_map test_perfect_hash_map.cpp)
target_link_libraries(test_perfect_hash_map PRIVATE Halide::Plugin)

add_halide_test(test_perfect_hash_map
                GROUPS autoschedulers_cpu)
