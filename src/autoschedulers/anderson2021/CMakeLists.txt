##
# Resources for the autoscheduler library
##

set(COMMON_DIR "${Halide_SOURCE_DIR}/src/autoschedulers/common")

# weights
set(WF_CPP baseline.cpp)
configure_file(baseline.weights baseline.weights COPYONLY)
add_custom_command(OUTPUT ${WF_CPP}
                   COMMAND binary2cpp baseline_weights < baseline.weights > ${WF_CPP}
                   DEPENDS baseline.weights binary2cpp
                   VERBATIM)

# cost_model, train_cost_model
add_executable(anderson2021_cost_model.generator cost_model_generator.cpp)
target_link_libraries(anderson2021_cost_model.generator PRIVATE Halide::Halide Halide::Generator)

add_halide_library(anderson2021_cost_model FROM anderson2021_cost_model.generator
                   GENERATOR cost_model
                   FUNCTION_NAME cost_model
                   TARGETS cmake)
add_halide_library(anderson2021_train_cost_model FROM anderson2021_cost_model.generator
                   GENERATOR train_cost_model
                   FUNCTION_NAME train_cost_model
                   TARGETS cmake
                   USE_RUNTIME anderson2021_cost_model.runtime)

## retrain_cost_model
add_executable(anderson2021_retrain_cost_model
               DefaultCostModel.cpp
               ${COMMON_DIR}/Weights.cpp
               retrain_cost_model.cpp
               ${WF_CPP})
target_include_directories(anderson2021_retrain_cost_model PRIVATE "${Halide_SOURCE_DIR}/src/autoschedulers/anderson2021")
target_link_libraries(anderson2021_retrain_cost_model PRIVATE ASLog anderson2021_cost_model
    anderson2021_train_cost_model Halide::Halide Halide::Plugin)

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
                  ${COMMON_DIR}/Weights.cpp
                  ${WF_CPP})

target_include_directories(Halide_Anderson2021 PUBLIC "${Halide_SOURCE_DIR}/src/autoschedulers/anderson2021")
target_link_libraries(Halide_Anderson2021 PRIVATE ASLog ParamParser
    anderson2021_cost_model anderson2021_train_cost_model)

##
# Tests and demos
# TODO(#4053): move these to a separate folder since they're tests.
##

# =================================================================

add_executable(anderson2021_demo.generator ${COMMON_DIR}/demo_generator.cpp)
target_link_libraries(anderson2021_demo.generator PRIVATE Halide::Halide Halide::Generator)

add_halide_library(anderson2021_demo FROM anderson2021_demo.generator
                    GENERATOR demo
                    FUNCTION_NAME demo
                    TARGETS cmake
                    FEATURES cuda cuda_capability_70
                    AUTOSCHEDULER Halide::Anderson2021
                    REGISTRATION DEMO_REGISTRATION_FILE)

add_executable(anderson2021_demo_apps_autoscheduler ${DEMO_REGISTRATION_FILE})
target_link_libraries(anderson2021_demo_apps_autoscheduler PRIVATE anderson2021_demo Halide::RunGenMain)

add_test(NAME anderson2021_demo_apps_autoscheduler
         COMMAND anderson2021_demo_apps_autoscheduler --benchmarks=all --benchmark_min_time=1 --estimate_all)
set_tests_properties(anderson2021_demo_apps_autoscheduler
                     PROPERTIES
                     LABELS Anderson2021)

## =================================================================

add_executable(anderson2021_included_schedule_file.generator ${COMMON_DIR}/included_schedule_file_generator.cpp)
target_include_directories(anderson2021_included_schedule_file.generator PRIVATE "${Halide_SOURCE_DIR}/src/autoschedulers/anderson2021")
target_link_libraries(anderson2021_included_schedule_file.generator PRIVATE
    Halide::Halide Halide::Generator)

add_halide_library(anderson2021_included_schedule_file FROM
    anderson2021_included_schedule_file.generator
                    GENERATOR included_schedule_file
                    FUNCTION_NAME included_schedule_file
                    TARGETS cmake
                   FEATURES cuda cuda_capability_70
                   AUTOSCHEDULER Halide::Anderson2021
                   REGISTRATION included_schedule_reg)

add_executable(anderson2021_demo_included_schedule_file ${included_schedule_reg})
target_link_libraries(anderson2021_demo_included_schedule_file PRIVATE anderson2021_included_schedule_file Halide::RunGenMain)

add_test(NAME anderson2021_demo_included_schedule_file
         COMMAND anderson2021_demo_included_schedule_file --benchmarks=all --benchmark_min_time=1 --estimate_all)
set_tests_properties(anderson2021_demo_included_schedule_file
                     PROPERTIES
                     LABELS Anderson2021)

## ====================================================
## Auto-tuning support utilities.
## TODO(#4053): implement auto-tuning support in CMake?

add_executable(anderson2021_featurization_to_sample ${COMMON_DIR}/featurization_to_sample.cpp)

add_executable(anderson2021_get_host_target ${COMMON_DIR}/get_host_target.cpp)
target_link_libraries(anderson2021_get_host_target PRIVATE Halide::Halide)

add_executable(anderson2021_weightsdir_to_weightsfile ${COMMON_DIR}/weightsdir_to_weightsfile.cpp ${COMMON_DIR}/Weights.cpp)
target_include_directories(anderson2021_weightsdir_to_weightsfile PRIVATE "${Halide_SOURCE_DIR}/src/autoschedulers/anderson2021" ${COMMON_DIR})
target_link_libraries(anderson2021_weightsdir_to_weightsfile PRIVATE Halide::Runtime)

# =================================================================
# Smaller tests

if (BUILD_SHARED_LIBS)
    add_executable(anderson2021_test_apps_autoscheduler test.cpp)
    target_link_libraries(anderson2021_test_apps_autoscheduler PRIVATE
        Halide::Halide Halide::Tools ${CMAKE_DL_LIBS})

    add_test(NAME anderson2021_test_apps_autoscheduler
             COMMAND anderson2021_test_apps_autoscheduler $<TARGET_FILE:Halide_Anderson2021>)

    set_tests_properties(anderson2021_test_apps_autoscheduler PROPERTIES
                         LABELS "Anderson2021;multithreaded"
                         ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:Halide_Anderson2021>:$ENV{LD_LIBRARY_PATH};HL_TARGET=${Halide_TARGET}")
endif ()

##

add_executable(anderson2021_test_perfect_hash_map ${COMMON_DIR}/test_perfect_hash_map.cpp)

add_test(NAME anderson2021_test_perfect_hash_map COMMAND test_perfect_hash_map)
set_tests_properties(anderson2021_test_perfect_hash_map
                     PROPERTIES
                     LABELS Anderson2021)

##

add_executable(anderson2021_test_function_dag ${COMMON_DIR}/test_function_dag.cpp FunctionDAG.cpp)
target_include_directories(anderson2021_test_function_dag PRIVATE "${Halide_SOURCE_DIR}/src/autoschedulers/anderson2021")
target_link_libraries(anderson2021_test_function_dag PRIVATE ASLog Halide::Halide Halide::Tools Halide::Plugin)

add_test(NAME anderson2021_test_function_dag COMMAND anderson2021_test_function_dag)
set_tests_properties(anderson2021_test_function_dag
                     PROPERTIES
                     LABELS Anderson2021)

add_executable(anderson2021_test_bounds test/bounds.cpp FunctionDAG.cpp LoopNest.cpp GPULoopInfo.cpp Tiling.cpp)
target_include_directories(anderson2021_test_bounds PRIVATE "${Halide_SOURCE_DIR}/src/autoschedulers/anderson2021")
target_link_libraries(anderson2021_test_bounds PRIVATE ASLog Halide::Halide Halide::Tools Halide::Plugin)

add_test(NAME anderson2021_test_bounds COMMAND anderson2021_test_bounds)
set_tests_properties(anderson2021_test_bounds
                     PROPERTIES
                     LABELS Anderson2021)

add_executable(anderson2021_test_parser test/parser.cpp)
target_include_directories(anderson2021_test_parser PRIVATE "${Halide_SOURCE_DIR}/src/autoschedulers/anderson2021")
target_link_libraries(anderson2021_test_parser PRIVATE ASLog Halide::Halide Halide::Tools Halide::Plugin)

add_test(NAME anderson2021_test_parser COMMAND anderson2021_test_parser)
set_tests_properties(anderson2021_test_parser
                     PROPERTIES
                     LABELS Anderson2021)

add_executable(anderson2021_test_state test/state.cpp FunctionDAG.cpp LoopNest.cpp GPULoopInfo.cpp State.cpp Tiling.cpp)
target_include_directories(anderson2021_test_state PRIVATE "${Halide_SOURCE_DIR}/src/autoschedulers/anderson2021")
target_link_libraries(anderson2021_test_state PRIVATE ASLog Halide::Halide Halide::Tools Halide::Plugin)

add_test(NAME anderson2021_test_state COMMAND anderson2021_test_state)
set_tests_properties(anderson2021_test_state
                     PROPERTIES
                     LABELS Anderson2021)

add_executable(anderson2021_test_storage_strides test/storage_strides.cpp FunctionDAG.cpp LoopNest.cpp GPULoopInfo.cpp State.cpp Tiling.cpp)
target_include_directories(anderson2021_test_storage_strides PRIVATE "${Halide_SOURCE_DIR}/src/autoschedulers/anderson2021")
target_link_libraries(anderson2021_test_storage_strides PRIVATE ASLog Halide::Halide Halide::Tools Halide::Plugin)

add_test(NAME anderson2021_test_storage_strides COMMAND anderson2021_test_storage_strides)
set_tests_properties(anderson2021_test_storage_strides
                     PROPERTIES
                     LABELS Anderson2021)

add_executable(anderson2021_test_thread_info test/thread_info.cpp LoopNest.cpp
    FunctionDAG.cpp GPULoopInfo.cpp Tiling.cpp)
target_include_directories(anderson2021_test_thread_info PRIVATE "${Halide_SOURCE_DIR}/src/autoschedulers/anderson2021")
target_link_libraries(anderson2021_test_thread_info PRIVATE ASLog Halide::Halide Halide::Tools Halide::Plugin)

add_test(NAME anderson2021_test_thread_info COMMAND anderson2021_test_thread_info)
set_tests_properties(anderson2021_test_thread_info
                     PROPERTIES
                     LABELS Anderson2021)

add_executable(anderson2021_test_tiling test/tiling.cpp Tiling.cpp)
target_include_directories(anderson2021_test_tiling PRIVATE "${Halide_SOURCE_DIR}/src/autoschedulers/anderson2021")
target_link_libraries(anderson2021_test_tiling PRIVATE ASLog Halide::Halide Halide::Tools Halide::Plugin)

add_test(NAME anderson2021_test_tiling COMMAND anderson2021_test_tiling)
set_tests_properties(anderson2021_test_tiling
                     PROPERTIES
                     LABELS Anderson2021)
