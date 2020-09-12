##
# Resources for the autoscheduler library
##

# weights
set(WF_CPP baseline.cpp)
configure_file(baseline.weights baseline.weights COPYONLY)
add_custom_command(OUTPUT ${WF_CPP}
                   COMMAND binary2cpp baseline_weights < baseline.weights > ${WF_CPP}
                   DEPENDS baseline.weights binary2cpp
                   VERBATIM)

# cost_model, train_cost_model
add_executable(cost_model.generator cost_model_generator.cpp)
target_link_libraries(cost_model.generator PRIVATE Halide::Generator)

add_halide_library(cost_model FROM cost_model.generator)
add_halide_library(train_cost_model FROM cost_model.generator
                   USE_RUNTIME cost_model.runtime)

# retrain_cost_model
add_executable(retrain_cost_model
               ASLog.cpp
               DefaultCostModel.cpp
               Weights.cpp
               retrain_cost_model.cpp
               ${WF_CPP})
target_link_libraries(retrain_cost_model PRIVATE cost_model train_cost_model Halide::Halide Halide::Plugin)

##
# Main autoscheduler library
##

add_autoscheduler(NAME Adams2019
                  SOURCES
                  ASLog.cpp
                  AutoSchedule.cpp
                  DefaultCostModel.cpp
                  FunctionDAG.cpp
                  LoopNest.cpp
                  Weights.cpp
                  ${WF_CPP})

target_link_libraries(Halide_Adams2019 PRIVATE cost_model train_cost_model)

##
# Tests and demos
# TODO(#4053): move these to a separate folder since they're tests.
##

# =================================================================

add_executable(demo.generator demo_generator.cpp)
target_link_libraries(demo.generator PRIVATE Halide::Generator)

add_halide_library(demo FROM demo.generator
                   AUTOSCHEDULER Halide::Adams2019
                   REGISTRATION DEMO_REGISTRATION_FILE)

add_executable(demo_apps_autoscheduler ${DEMO_REGISTRATION_FILE})
target_link_libraries(demo_apps_autoscheduler PRIVATE demo Halide::RunGenMain)

add_test(NAME demo_apps_autoscheduler
         COMMAND demo_apps_autoscheduler --benchmarks=all --benchmark_min_time=1 --estimate_all)

set_tests_properties(demo_apps_autoscheduler
                     PROPERTIES
                     LABELS Adams2019
                     ENVIRONMENT "HL_TARGET=${Halide_TARGET}")

# =================================================================

add_executable(included_schedule_file.generator included_schedule_file_generator.cpp)
target_link_libraries(included_schedule_file.generator PRIVATE Halide::Generator)

add_halide_library(included_schedule_file FROM included_schedule_file.generator
                   AUTOSCHEDULER Halide::Adams2019
                   REGISTRATION included_schedule_reg)

add_executable(demo_included_schedule_file ${included_schedule_reg})
target_link_libraries(demo_included_schedule_file PRIVATE included_schedule_file Halide::RunGenMain)

add_test(NAME demo_included_schedule_file
         COMMAND demo_included_schedule_file --benchmarks=all --benchmark_min_time=1 --estimate_all)

set_tests_properties(demo_included_schedule_file
                     PROPERTIES
                     LABELS Adams2019
                     ENVIRONMENT "HL_TARGET=${Halide_TARGET}")

# ====================================================
# Auto-tuning support utilities.
# TODO(#4053): implement auto-tuning support in CMake?

add_executable(featurization_to_sample featurization_to_sample.cpp)

add_executable(get_host_target get_host_target.cpp)
target_link_libraries(get_host_target PRIVATE Halide::Halide)

add_executable(weightsdir_to_weightsfile weightsdir_to_weightsfile.cpp Weights.cpp)
target_link_libraries(weightsdir_to_weightsfile PRIVATE Halide::Runtime)

# =================================================================
# Smaller tests

if (BUILD_SHARED_LIBS)
    add_executable(test_apps_autoscheduler test.cpp)
    target_link_libraries(test_apps_autoscheduler PRIVATE Halide::Halide Halide::Tools ${CMAKE_DL_LIBS})

    add_test(NAME test_apps_autoscheduler
             COMMAND test_apps_autoscheduler $<TARGET_FILE:Halide_Adams2019>)

    set_tests_properties(test_apps_autoscheduler PROPERTIES
                         LABELS Adams2019
                         ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:Halide_Adams2019>;HL_TARGET=${Halide_TARGET}")
endif ()

##

add_executable(test_perfect_hash_map test_perfect_hash_map.cpp)

add_test(NAME test_perfect_hash_map COMMAND test_perfect_hash_map)
set_tests_properties(test_perfect_hash_map
                     PROPERTIES
                     LABELS Adams2019
                     ENVIRONMENT "HL_TARGET=${Halide_TARGET}")

##

add_executable(test_function_dag test_function_dag.cpp FunctionDAG.cpp ASLog.cpp)
target_link_libraries(test_function_dag PRIVATE Halide::Halide Halide::Tools Halide::Plugin)

add_test(NAME test_function_dag COMMAND test_function_dag)
set_tests_properties(test_function_dag
                     PROPERTIES
                     LABELS Adams2019
                     ENVIRONMENT "HL_TARGET=${Halide_TARGET}")
