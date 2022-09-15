##
# Build rules for the Adams2019 autoscheduler library
##

function(add_adams2019_test NAME)
    set(options "")
    set(oneValueArgs ENVIRONMENT)
    set(multiValueArgs COMMAND LABELS)
    cmake_parse_arguments(ARGS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT ARGS_COMMAND)
        set(ARGS_COMMAND ${NAME})
    endif()

    if (NOT ARGS_LABELS)
        set(ARGS_LABELS "")
    endif()
    list(APPEND ARGS_LABELS Adams2019)
    list(APPEND ARGS_LABELS auto_schedule)

    add_test(NAME ${NAME}
             COMMAND ${ARGS_COMMAND})
    set_tests_properties(${NAME}
                         PROPERTIES
                         LABELS "${ARGS_LABELS}"
                         ENVIRONMENT "${ENVIRONMENT}")
endfunction()


# weights
set(WF_CPP baseline.cpp)
configure_file(baseline.weights baseline.weights COPYONLY)
add_custom_command(OUTPUT ${WF_CPP}
                   COMMAND binary2cpp baseline_weights < baseline.weights > ${WF_CPP}
                   DEPENDS baseline.weights binary2cpp
                   VERBATIM)

add_library(adams2019_weights_obj OBJECT ${WF_CPP})

# cost_model, train_cost_model
add_executable(adams2019_cost_model.generator cost_model_generator.cpp)
target_link_libraries(adams2019_cost_model.generator PRIVATE Halide::Generator)

add_halide_library(adams2019_cost_model FROM adams2019_cost_model.generator
                   GENERATOR cost_model
                   FUNCTION_NAME cost_model
                   TARGETS cmake)
add_halide_library(adams2019_train_cost_model FROM adams2019_cost_model.generator
                   GENERATOR train_cost_model
                   FUNCTION_NAME train_cost_model
                   TARGETS cmake
                   USE_RUNTIME adams2019_cost_model.runtime)

# retrain_cost_model
add_executable(adams2019_retrain_cost_model
               DefaultCostModel.cpp
               Weights.cpp
               retrain_cost_model.cpp
               $<TARGET_OBJECTS:adams2019_weights_obj>)
target_link_libraries(adams2019_retrain_cost_model PRIVATE ASLog adams2019_cost_model adams2019_train_cost_model Halide::Halide Halide::Plugin)

##
# Main autoscheduler library
##

add_autoscheduler(NAME Adams2019
                  SOURCES
                  AutoSchedule.cpp
                  Cache.cpp
                  DefaultCostModel.cpp
                  FunctionDAG.cpp
                  LoopNest.cpp
                  State.cpp
                  Weights.cpp
                  $<TARGET_OBJECTS:adams2019_weights_obj>)

target_link_libraries(Halide_Adams2019 PRIVATE ASLog ParamParser adams2019_cost_model adams2019_train_cost_model)

##
# Tests and demos
# TODO(#4053): move these to a separate folder since they're tests.
##

# =================================================================

add_executable(adams2019_demo.generator demo_generator.cpp)
target_link_libraries(adams2019_demo.generator PRIVATE Halide::Generator)

add_halide_library(adams2019_demo FROM adams2019_demo.generator
                   GENERATOR demo
                   TARGETS cmake
                   AUTOSCHEDULER Halide::Adams2019
                   REGISTRATION DEMO_REGISTRATION_FILE)

add_executable(adams2019_demo_apps_autoscheduler ${DEMO_REGISTRATION_FILE})
target_link_libraries(adams2019_demo_apps_autoscheduler PRIVATE adams2019_demo Halide::RunGenMain)

add_adams2019_test(adams2019_demo_apps_autoscheduler
                   COMMAND adams2019_demo_apps_autoscheduler --benchmarks=all --benchmark_min_time=1 --estimate_all)

# =================================================================

add_executable(adams2019_included_schedule_file.generator included_schedule_file_generator.cpp)
target_link_libraries(adams2019_included_schedule_file.generator PRIVATE Halide::Generator)

add_halide_library(adams2019_included_schedule_file FROM adams2019_included_schedule_file.generator
                   GENERATOR included_schedule_file
                   TARGETS cmake
                   AUTOSCHEDULER Halide::Adams2019
                   REGISTRATION adams2019_included_schedule_reg)

add_executable(adams2019_demo_included_schedule_file ${adams2019_included_schedule_reg})
target_link_libraries(adams2019_demo_included_schedule_file PRIVATE adams2019_included_schedule_file Halide::RunGenMain)

add_adams2019_test(adams2019_demo_included_schedule_file
                   COMMAND adams2019_demo_included_schedule_file --benchmarks=all --benchmark_min_time=1 --estimate_all)

# ====================================================
# Auto-tuning support utilities.
# TODO(#4053): implement auto-tuning support in CMake?

add_executable(adams2019_featurization_to_sample featurization_to_sample.cpp)

add_executable(adams2019_get_host_target get_host_target.cpp)
target_link_libraries(adams2019_get_host_target PRIVATE Halide::Halide)

add_executable(adams2019_weightsdir_to_weightsfile weightsdir_to_weightsfile.cpp Weights.cpp)
target_link_libraries(adams2019_weightsdir_to_weightsfile PRIVATE Halide::Runtime)

# =================================================================
# Smaller tests

if (BUILD_SHARED_LIBS)
    add_executable(adams2019_test_apps_autoscheduler test.cpp)
    target_link_libraries(adams2019_test_apps_autoscheduler PRIVATE Halide::Halide Halide::Tools ${CMAKE_DL_LIBS})

    add_adams2019_test(adams2019_test_apps_autoscheduler
                       COMMAND adams2019_test_apps_autoscheduler $<TARGET_FILE:Halide_Adams2019> ${CMAKE_CURRENT_SOURCE_DIR}/baseline.weights
                       LABELS multithreaded
                       ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:Halide_Adams2019>:$ENV{LD_LIBRARY_PATH}")
endif ()

##

add_executable(adams2019_test_perfect_hash_map test_perfect_hash_map.cpp)

add_adams2019_test(adams2019_test_perfect_hash_map)

##

add_executable(adams2019_test_function_dag test_function_dag.cpp FunctionDAG.cpp)
target_link_libraries(adams2019_test_function_dag PRIVATE ASLog Halide::Halide Halide::Tools Halide::Plugin)

add_adams2019_test(adams2019_test_function_dag)
