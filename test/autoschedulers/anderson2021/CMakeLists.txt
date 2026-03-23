##
# Tests for the Anderson2021 autoscheduler
##

set(ANDERSON2021_SRC_DIR "${Halide_SOURCE_DIR}/src/autoschedulers/anderson2021")

# =================================================================

# demo_generator.cpp
add_halide_generator(anderson2021_demo.generator
                     SOURCES demo_generator.cpp)

add_halide_library(anderson2021_demo FROM anderson2021_demo.generator
                   GENERATOR demo
                   TARGETS cmake
                   FEATURES cuda cuda_capability_70
                   AUTOSCHEDULER Halide::Anderson2021
                   REGISTRATION DEMO_REGISTRATION_FILE)

add_executable(anderson2021_demo_apps_autoscheduler ${DEMO_REGISTRATION_FILE})
target_link_libraries(anderson2021_demo_apps_autoscheduler PRIVATE anderson2021_demo Halide::RunGenMain)
add_dependencies(anderson2021_demo_apps_autoscheduler Halide::Anderson2021)

add_halide_test(anderson2021_demo_apps_autoscheduler
                COMMAND anderson2021_demo_apps_autoscheduler --benchmarks=all --benchmark_min_time=1 --estimate_all --success --skip_bad_environment
                GROUPS anderson2021 autoschedulers_cuda multithreaded)

# =================================================================

# included_schedule_file_generator.cpp
add_halide_generator(anderson2021_included_schedule_file.generator
                     SOURCES included_schedule_file_generator.cpp)

add_halide_library(anderson2021_included_schedule_file FROM anderson2021_included_schedule_file.generator
                   GENERATOR included_schedule_file
                   TARGETS cmake
                   FEATURES cuda cuda_capability_70
                   AUTOSCHEDULER Halide::Anderson2021
                   REGISTRATION anderson2021_included_schedule_reg)

add_executable(anderson2021_demo_included_schedule_file ${anderson2021_included_schedule_reg})
target_link_libraries(anderson2021_demo_included_schedule_file PRIVATE anderson2021_included_schedule_file Halide::RunGenMain)
add_dependencies(anderson2021_demo_included_schedule_file Halide::Anderson2021)

add_halide_test(anderson2021_demo_included_schedule_file
                COMMAND anderson2021_demo_included_schedule_file --benchmarks=all --benchmark_min_time=1 --estimate_all --success --skip_bad_environment
                GROUPS anderson2021 autoschedulers_cuda multithreaded)

# =================================================================
# Smaller tests

# test.cpp (public API test)
add_executable(anderson2021_test_apps_autoscheduler test.cpp)
target_link_libraries(anderson2021_test_apps_autoscheduler PRIVATE Halide::Halide Halide::Tools ${CMAKE_DL_LIBS})
add_dependencies(anderson2021_test_apps_autoscheduler Halide::Anderson2021)

add_halide_test(anderson2021_test_apps_autoscheduler
                COMMAND anderson2021_test_apps_autoscheduler $<TARGET_FILE:Halide_Anderson2021>
                GROUPS anderson2021 autoschedulers_cuda multithreaded)
set_tests_properties(anderson2021_test_apps_autoscheduler PROPERTIES
                     ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:Halide_Anderson2021>")

# =================================================================
# Internal unit tests

# Helper to reduce boilerplate for anderson2021 internal tests.
# Takes the source file name (e.g. test_foo.cpp) as the first argument
# so that it appears on its own line for the CMake file list checker.
function(_add_anderson2021_internal_test SRC)
    cmake_parse_arguments(ARGS "" "" "EXTRA_SRCS" ${ARGN})

    cmake_path(GET SRC STEM NAME)

    set(SRCS ${SRC})
    foreach (src IN LISTS ARGS_EXTRA_SRCS)
        list(APPEND SRCS "${ANDERSON2021_SRC_DIR}/${src}")
    endforeach ()

    add_executable(anderson2021_${NAME} ${SRCS})
    target_include_directories(anderson2021_${NAME} PRIVATE
                               ${ANDERSON2021_SRC_DIR}
                               ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(anderson2021_${NAME} PRIVATE Halide::Plugin)
    add_dependencies(anderson2021_${NAME} Halide::Anderson2021)

    add_halide_test(anderson2021_${NAME}
                    GROUPS anderson2021 autoschedulers_cuda)
    # Workaround for nvidia driver + ASAN incompatibility
    # https://forums.developer.nvidia.com/t/cuda-runtime-library-and-addresssanitizer-incompatibilty/63145
    set_tests_properties(anderson2021_${NAME} PROPERTIES
                         ENVIRONMENT_MODIFICATION "ASAN_OPTIONS=set:protect_shadow_gap=0")
endfunction()

_add_anderson2021_internal_test(test_function_dag.cpp EXTRA_SRCS FunctionDAG.cpp)
_add_anderson2021_internal_test(test_bounds.cpp EXTRA_SRCS FunctionDAG.cpp LoopNest.cpp GPULoopInfo.cpp Tiling.cpp)
_add_anderson2021_internal_test(test_parser.cpp)
_add_anderson2021_internal_test(test_state.cpp EXTRA_SRCS FunctionDAG.cpp LoopNest.cpp GPULoopInfo.cpp State.cpp Tiling.cpp)
_add_anderson2021_internal_test(test_storage_strides.cpp EXTRA_SRCS FunctionDAG.cpp LoopNest.cpp GPULoopInfo.cpp State.cpp Tiling.cpp)
_add_anderson2021_internal_test(test_thread_info.cpp EXTRA_SRCS LoopNest.cpp FunctionDAG.cpp GPULoopInfo.cpp Tiling.cpp)
_add_anderson2021_internal_test(test_tiling.cpp EXTRA_SRCS Tiling.cpp)
