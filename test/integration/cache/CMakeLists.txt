cmake_minimum_required(VERSION 3.28)
project(cache CXX)

enable_testing()

find_package(Halide REQUIRED)

# This project must be configured with a cache directory so that the shipped
# add_halide_library rules pass HL_CACHE_DIR to the generator process.
if (NOT Halide_CACHE_DIR)
    message(FATAL_ERROR "The cache integration test requires -DHalide_CACHE_DIR")
endif ()

add_executable(add_gen add.cpp)
target_link_libraries(add_gen PRIVATE Halide::Generator)

add_halide_library(add FROM add_gen REGISTRATION add_registration)

# Building and running this app validates that the artifacts produced (or
# restored from the cache) actually link and execute.
add_executable(run_add ${add_registration})
target_link_libraries(run_add PRIVATE Halide::RunGenMain add)

add_test(NAME benchmark COMMAND run_add --benchmarks=all --output_extents=[64,64])
set_tests_properties(benchmark PROPERTIES PASS_REGULAR_EXPRESSION "Best output throughput")

# `add` is part of ALL, so it is built (running the generator under
# HL_CACHE_DIR) before ctest runs. This check confirms the cache was populated,
# i.e. that HL_CACHE_DIR was routed through the shipped helpers.
add_test(
    NAME check_cache
    COMMAND
        ${CMAKE_COMMAND}
        "-DCACHE_DIR=${Halide_CACHE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_cache.cmake"
)
