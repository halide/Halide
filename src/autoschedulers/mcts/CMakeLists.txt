##
# Resources for the autoscheduler library
##

# weights
#set(WF_CPP baseline.cpp)
#configure_file(baseline.weights baseline.weights COPYONLY)
#add_custom_command(OUTPUT ${WF_CPP}
#                   COMMAND binary2cpp baseline_weights < baseline.weights > ${WF_CPP}
#                   DEPENDS baseline.weights binary2cpp
#                   VERBATIM)

# cost_model, train_cost_model
#add_executable(mcts_cost_model.generator cost_model_generator.cpp)
#target_link_libraries(mcts_cost_model.generator PRIVATE Halide::Generator)

#add_halide_library(cost_model FROM mcts_cost_model.generator
#                   GENERATOR cost_model
#		   HEADER_NAME cost_model
#		   TARGETS cmake)
#add_halide_library(train_cost_model FROM mcts_cost_model.generator
#                   GENERATOR train_cost_model
#		   HEADER_NAME train_cost_model
#                   TARGETS cmake
#                   USE_RUNTIME cost_model.runtime)

# retrain_cost_model
#add_executable(mcts_retrain_cost_model
#               ASLog.cpp
#               DefaultCostModel.cpp
#               Weights.cpp
#               retrain_cost_model.cpp
#               ${WF_CPP})
#target_link_libraries(mcts_retrain_cost_model PRIVATE cost_model train_cost_model Halide::Halide Halide::Plugin)

##
# Main autoscheduler library
##

add_autoscheduler(NAME mcts
                  SOURCES
                  ASLog.cpp
                  AutoSchedule.cpp
                  CPU_State.cpp
                  DefaultCostModel.cpp
                  FunctionDAG.cpp
                  LoopNest.cpp
                  Weights.cpp
                  ${WF_CPP})

target_link_libraries(Halide_mcts PRIVATE ../resource/cost_model ../resource/train_cost_model)

##
# Tests and demos
# TODO(#4053): move these to a separate folder since they're tests.
##

# =================================================================

add_executable(mcts_demo.generator demo_generator.cpp)
target_link_libraries(mcts_demo.generator PRIVATE Halide::Generator)

add_halide_library(mcts_demo FROM mcts_demo.generator
	           GENERATOR demo
		   HEADER_NAME demo
		   TARGETS cmake
                   AUTOSCHEDULER Halide::mcts
                   REGISTRATION DEMO_REGISTRATION_FILE)

add_executable(mcts_demo_apps_autoscheduler ${DEMO_REGISTRATION_FILE})
target_link_libraries(mcts_demo_apps_autoscheduler PRIVATE mcts_demo Halide::RunGenMain)

add_test(NAME mcts_demo_apps_autoscheduler
         COMMAND mcts_demo_apps_autoscheduler --benchmarks=all --benchmark_min_time=1 --estimate_all)

set_tests_properties(mcts_demo_apps_autoscheduler
                     PROPERTIES
                     LABELS mcts
                     ENVIRONMENT "HL_TARGET=${Halide_TARGET}")

# =================================================================

add_executable(mcts_included_schedule_file.generator included_schedule_file_generator.cpp)
target_link_libraries(mcts_included_schedule_file.generator PRIVATE Halide::Generator)

add_halide_library(mcts_included_schedule_file FROM mcts_included_schedule_file.generator
                   GENERATOR included_schedule_file
		   HEADER_NAME included_schedule_file
		   TARGETS cmake
                   AUTOSCHEDULER Halide::mcts
                   REGISTRATION included_schedule_reg)

add_executable(mcts_demo_included_schedule_file ${included_schedule_reg})
target_link_libraries(mcts_demo_included_schedule_file PRIVATE mcts_included_schedule_file Halide::RunGenMain)

add_test(NAME mcts_demo_included_schedule_file
         COMMAND mcts_demo_included_schedule_file --benchmarks=all --benchmark_min_time=1 --estimate_all)

set_tests_properties(mcts_demo_included_schedule_file
                     PROPERTIES
                     LABELS mcts
                     ENVIRONMENT "HL_TARGET=${Halide_TARGET}")

# ====================================================
# Auto-tuning support utilities.
# TODO(#4053): implement auto-tuning support in CMake?

add_executable(mcts_featurization_to_sample featurization_to_sample.cpp)

add_executable(mcts_get_host_target get_host_target.cpp)
target_link_libraries(mcts_get_host_target PRIVATE Halide::Halide)

add_executable(mcts_weightsdir_to_weightsfile weightsdir_to_weightsfile.cpp Weights.cpp)
target_link_libraries(mcts_weightsdir_to_weightsfile PRIVATE Halide::Runtime)

# =================================================================
# Smaller tests

if (BUILD_SHARED_LIBS)
    add_executable(mcts_test_apps_autoscheduler test.cpp)
    target_link_libraries(mcts_test_apps_autoscheduler PRIVATE Halide::Halide Halide::Tools ${CMAKE_DL_LIBS})

    add_test(NAME mcts_test_apps_autoscheduler
             COMMAND mcts_test_apps_autoscheduler $<TARGET_FILE:Halide_mcts>)

    set_tests_properties(mcts_test_apps_autoscheduler PROPERTIES
                         LABELS mcts
                         ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:Halide_mcts>:$ENV{LD_LIBRARY_PATH};HL_TARGET=${Halide_TARGET}")
endif ()

##

add_executable(mcts_test_perfect_hash_map test_perfect_hash_map.cpp)

add_test(NAME mcts_test_perfect_hash_map COMMAND mcts_test_perfect_hash_map)
set_tests_properties(mcts_test_perfect_hash_map
                     PROPERTIES
                     LABELS mcts
                     ENVIRONMENT "HL_TARGET=${Halide_TARGET}")

##

add_executable(mcts_test_function_dag test_function_dag.cpp FunctionDAG.cpp ASLog.cpp)
target_link_libraries(mcts_test_function_dag PRIVATE Halide::Halide Halide::Tools Halide::Plugin)

add_test(NAME mcts_test_function_dag COMMAND mcts_test_function_dag)
set_tests_properties(mcts_test_function_dag
                     PROPERTIES
                     LABELS mcts
                     ENVIRONMENT "HL_TARGET=${Halide_TARGET}")
