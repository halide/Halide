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

add_library(anderson2021_weights_obj OBJECT ${WF_CPP})

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
if (WITH_UTILS)
    add_executable(anderson2021_retrain_cost_model
                   DefaultCostModel.cpp
                   Weights.cpp
                   retrain_cost_model.cpp
                   $<TARGET_OBJECTS:anderson2021_weights_obj>)
    target_include_directories(anderson2021_retrain_cost_model PRIVATE "${Halide_SOURCE_DIR}/src/autoschedulers/anderson2021")
    target_link_libraries(anderson2021_retrain_cost_model PRIVATE ASLog anderson2021_cost_model
        anderson2021_train_cost_model Halide::Halide Halide::Plugin)
endif ()

###
## Main autoscheduler library
###

add_autoscheduler(
    NAME Anderson2021
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
    $<TARGET_OBJECTS:anderson2021_weights_obj>
)

target_include_directories(Halide_Anderson2021 PRIVATE "${Halide_SOURCE_DIR}/src/autoschedulers/anderson2021")
target_link_libraries(Halide_Anderson2021 PRIVATE ASLog ParamParser
    anderson2021_cost_model anderson2021_train_cost_model)

## ====================================================
if (WITH_UTILS)
    add_executable(anderson2021_weightsdir_to_weightsfile weightsdir_to_weightsfile.cpp Weights.cpp)
    target_include_directories(anderson2021_weightsdir_to_weightsfile PRIVATE ${COMMON_DIR})
    target_link_libraries(anderson2021_weightsdir_to_weightsfile PRIVATE Halide::Runtime)
endif ()

# =================================================================
# Tests for private/internal functionality of Anderson2021 (vs for public functionality,
# which is handled in tests/autoschedulers/anderson2021)

if (WITH_TESTS)
    ##

    add_executable(anderson2021_test_function_dag test_function_dag.cpp FunctionDAG.cpp)
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
endif()
