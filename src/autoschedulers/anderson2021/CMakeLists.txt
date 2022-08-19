##
# Resources for the autoscheduler library
##

add_compile_definitions(HALIDE_ALLOW_LEGACY_AUTOSCHEDULER_API)

set(COMMON_DIR "${Halide_SOURCE_DIR}/src/autoschedulers/common")
include_directories("${Halide_BINARY_DIR}/include/")
include_directories(${COMMON_DIR})
include_directories("${Halide_SOURCE_DIR}/src/autoschedulers/anderson2021/")

# weights
set(WF_CPP baseline.cpp)
configure_file(baseline.weights baseline.weights COPYONLY)
add_custom_command(OUTPUT ${WF_CPP}
                   COMMAND binary2cpp baseline_weights < baseline.weights > ${WF_CPP}
                   DEPENDS baseline.weights binary2cpp
                   VERBATIM)

# cost_model, train_cost_model
add_executable(anderson2021-cost_model.generator cost_model_generator.cpp)
target_link_libraries(cost_model.generator PRIVATE Halide::Halide Halide::Generator)

add_halide_library(anderson2021-cost_model FROM cost_model.generator
                   TARGETS cmake)
add_halide_library(anderson2021-train_cost_model FROM cost_model.generator
                   TARGETS cmake
                   USE_RUNTIME cost_model.runtime)

## retrain_cost_model
add_executable(anderson2021-retrain_cost_model
               DefaultCostModel.cpp
               Weights.cpp
               retrain_cost_model.cpp
               ${WF_CPP})
target_link_libraries(retrain_cost_model PRIVATE ASLog cost_model train_cost_model Halide::Halide Halide::Plugin)

###
## Main autoscheduler library
###

add_autoscheduler(NAME Anderson2021
                  SOURCES
                  AutoSchedule.cpp
                  DefaultCostModel.cpp
                  FunctionDAG.cpp
                  GPULoopInfo.cpp
                  LoopNest.cpp
                  SearchSpace.cpp
                  State.cpp
                  Tiling.cpp
                  Weights.cpp
                  ${WF_CPP})

target_link_libraries(Halide_Anderson2021 PRIVATE ASLog ParamParser cost_model train_cost_model)

##
# Tests and demos
# TODO(#4053): move these to a separate folder since they're tests.
##

# =================================================================

add_executable(anderson2021-demo.generator demo_generator.cpp)
target_link_libraries(demo.generator PRIVATE Halide::Generator)

add_halide_library(anderson2021-demo FROM demo.generator
                   TARGETS cmake
                   AUTOSCHEDULER Halide::Anderson2021
                   REGISTRATION DEMO_REGISTRATION_FILE)

add_executable(anderson2021-demo_apps_autoscheduler ${DEMO_REGISTRATION_FILE})
target_link_libraries(demo_apps_autoscheduler PRIVATE demo Halide::RunGenMain)

add_test(NAME demo_apps_autoscheduler
         COMMAND demo_apps_autoscheduler --benchmarks=all --benchmark_min_time=1 --estimate_all)

set_tests_properties(demo_apps_autoscheduler
                     PROPERTIES
                     LABELS Anderson2021
                     ENVIRONMENT "HL_TARGET=${Halide_TARGET}")

## =================================================================

add_executable(anderson2021-included_schedule_file.generator included_schedule_file_generator.cpp)
target_link_libraries(included_schedule_file.generator PRIVATE Halide::Generator)

add_halide_library(anderson2021-included_schedule_file FROM included_schedule_file.generator
                   TARGETS cmake
                   AUTOSCHEDULER Halide::Anderson2021
                   REGISTRATION included_schedule_reg)

add_executable(anderson2021-demo_included_schedule_file ${included_schedule_reg})
target_link_libraries(demo_included_schedule_file PRIVATE included_schedule_file Halide::RunGenMain)

add_test(NAME demo_included_schedule_file
         COMMAND demo_included_schedule_file --benchmarks=all --benchmark_min_time=1 --estimate_all)

set_tests_properties(demo_included_schedule_file
                     PROPERTIES
                     LABELS Anderson2021
                     ENVIRONMENT "HL_TARGET=${Halide_TARGET}")

## ====================================================
## Auto-tuning support utilities.
## TODO(#4053): implement auto-tuning support in CMake?

add_executable(anderson2021-featurization_to_sample ${COMMON_DIR}/featurization_to_sample.cpp)

add_executable(anderson2021-get_host_target ${COMMON_DIR}/get_host_target.cpp)
target_link_libraries(get_host_target PRIVATE Halide::Halide)

add_executable(anderson2021-weightsdir_to_weightsfile weightsdir_to_weightsfile.cpp Weights.cpp)
target_link_libraries(weightsdir_to_weightsfile PRIVATE Halide::Runtime)

# =================================================================
# Smaller tests

if (BUILD_SHARED_LIBS)
    add_executable(anderson2021-test_apps_autoscheduler test.cpp)
    target_link_libraries(test_apps_autoscheduler PRIVATE Halide::Halide Halide::Tools ${CMAKE_DL_LIBS})

    add_test(NAME test_apps_autoscheduler
             COMMAND test_apps_autoscheduler $<TARGET_FILE:Halide_Anderson2021>)

    set_tests_properties(test_apps_autoscheduler PROPERTIES
                         LABELS "Anderson2021;multithreaded"
                         ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:Halide_Anderson2021>:$ENV{LD_LIBRARY_PATH};HL_TARGET=${Halide_TARGET}")
endif ()

##

add_executable(anderson2021-test_perfect_hash_map ${COMMON_DIR}/test_perfect_hash_map.cpp)

add_test(NAME test_perfect_hash_map COMMAND test_perfect_hash_map)
set_tests_properties(test_perfect_hash_map
                     PROPERTIES
                     LABELS Anderson2021
                     ENVIRONMENT "HL_TARGET=${Halide_TARGET}")

##

add_executable(anderson2021-test_function_dag ${COMMON_DIR}/test_function_dag.cpp FunctionDAG.cpp)
target_link_libraries(anderson2021-test_function_dag PRIVATE ASLog Halide::Halide Halide::Tools Halide::Plugin)

add_test(NAME test_function_dag COMMAND test_function_dag)
set_tests_properties(test_function_dag
                     PROPERTIES
                     LABELS Anderson2021
                     ENVIRONMENT "HL_TARGET=${Halide_TARGET}")

add_executable(anderson2021-test_bounds test/bounds.cpp FunctionDAG.cpp LoopNest.cpp GPULoopInfo.cpp Tiling.cpp)
target_link_libraries(anderson2021-test_bounds PRIVATE ASLog Halide::Halide Halide::Tools Halide::Plugin)

add_test(NAME anderson2021-test_bounds COMMAND test_bounds)
set_tests_properties(anderson2021-test_bounds
                     PROPERTIES
                     LABELS Anderson2021
                     ENVIRONMENT "HL_TARGET=${Halide_TARGET}")

add_executable(anderson2021-test_parser test/parser.cpp)
target_link_libraries(anderson2021-test_parser PRIVATE ASLog Halide::Halide Halide::Tools Halide::Plugin)

add_test(NAME test_parser COMMAND anderson2021-test_parser)
set_tests_properties(test_parser
                     PROPERTIES
                     LABELS Anderson2021
                     ENVIRONMENT "HL_TARGET=${Halide_TARGET}")

add_executable(anderson2021-test_state test/state.cpp FunctionDAG.cpp LoopNest.cpp GPULoopInfo.cpp State.cpp Tiling.cpp)
target_link_libraries(anderson2021-test_state PRIVATE ASLog Halide::Halide Halide::Tools Halide::Plugin)

add_test(NAME test_state COMMAND anderson2021-test_state)
set_tests_properties(test_state
                     PROPERTIES
                     LABELS Anderson2021
                     ENVIRONMENT "HL_TARGET=${Halide_TARGET}")

add_executable(anderson2021-test_storage_strides test/storage_strides.cpp FunctionDAG.cpp LoopNest.cpp GPULoopInfo.cpp State.cpp Tiling.cpp)
target_link_libraries(anderson2021-test_storage_strides PRIVATE ASLog Halide::Halide Halide::Tools Halide::Plugin)

add_test(NAME test_storage_strides COMMAND anderson2021-test_storage_strides)
set_tests_properties(test_storage_strides
                     PROPERTIES
                     LABELS Anderson2021
                     ENVIRONMENT "HL_TARGET=${Halide_TARGET}")

add_executable(anderson2021-test_thread_info test/thread_info.cpp LoopNest.cpp)
target_link_libraries(anderson2021-test_thread_info PRIVATE ASLog Halide::Halide Halide::Tools Halide::Plugin)

add_test(NAME test_thread_info COMMAND anderson2021-test_thread_info)
set_tests_properties(test_thread_info
                     PROPERTIES
                     LABELS Anderson2021
                     ENVIRONMENT "HL_TARGET=${Halide_TARGET}")

add_executable(anderson2021-test_tiling test/tiling.cpp Tiling.cpp)
target_link_libraries(anderson2021-test_tiling PRIVATE ASLog Halide::Halide Halide::Tools Halide::Plugin)

add_test(NAME test_tiling COMMAND anderson2021-test_tiling)
set_tests_properties(test_tiling
                     PROPERTIES
                     LABELS Anderson2021
                     ENVIRONMENT "HL_TARGET=${Halide_TARGET}")
