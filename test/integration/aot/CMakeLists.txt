cmake_minimum_required(VERSION 3.22)
project(aot)

enable_testing()

find_package(Halide REQUIRED)

option(aot_USE_AUTOSCHEDULER "Use the autoscheduler" OFF)
if (aot_USE_AUTOSCHEDULER)
    set(extra_options AUTOSCHEDULER Halide::Adams2019)
endif ()

add_executable(add_gen add.cpp)
target_link_libraries(add_gen PRIVATE Halide::Generator)

add_halide_library(add FROM add_gen
                   ${extra_options}
                   REGISTRATION add_registration)

add_executable(run_add ${add_registration})
target_link_libraries(run_add PRIVATE Halide::RunGenMain add)

add_test(NAME benchmark
         COMMAND run_add --benchmarks=all --output_extents=[64,64])

set_tests_properties(benchmark PROPERTIES PASS_REGULAR_EXPRESSION "Best output throughput")
