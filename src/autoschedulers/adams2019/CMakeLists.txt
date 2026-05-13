##
# Build rules for the Adams2019 autoscheduler library
##

set(COMMON_DIR "${Halide_SOURCE_DIR}/src/autoschedulers/common")

# =================================================================
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
                   FEATURES[x86-64-osx] avx2 sse41
                   FEATURES[arm-64-osx] arm_dot_prod-arm_fp16)

add_halide_library(adams2019_train_cost_model FROM adams2019_cost_model.generator
                   GENERATOR train_cost_model
                   FUNCTION_NAME train_cost_model
                   FEATURES[x86-64-osx] avx2 sse41
                   FEATURES[arm-64-osx] arm_dot_prod-arm_fp16
                   USE_RUNTIME adams2019_cost_model.runtime)

# adams2019_retrain_cost_model
if (WITH_UTILS)
    add_executable(adams2019_retrain_cost_model
                   DefaultCostModel.cpp
                   Weights.cpp
                   retrain_cost_model.cpp
                   $<TARGET_OBJECTS:adams2019_weights_obj>)
    target_include_directories(adams2019_retrain_cost_model PRIVATE "${Halide_SOURCE_DIR}/src/autoschedulers/adams2019")
    target_link_libraries(adams2019_retrain_cost_model PRIVATE adams2019_cost_model adams2019_train_cost_model Halide::Plugin)
endif ()

# =================================================================
##
# Main autoscheduler library
##

add_autoscheduler(
    NAME Adams2019
    SOURCES
    AutoSchedule.cpp
    Cache.cpp
    DefaultCostModel.cpp
    FunctionDAG.cpp
    LoopNest.cpp
    State.cpp
    Weights.cpp
    $<TARGET_OBJECTS:adams2019_weights_obj>
)

target_include_directories(Halide_Adams2019 PRIVATE "${Halide_SOURCE_DIR}/src/autoschedulers/adams2019")
target_link_libraries(Halide_Adams2019 PRIVATE adams2019_cost_model adams2019_train_cost_model)

# ====================================================
# Auto-tuning support utilities.

if (WITH_UTILS)
    add_executable(adams2019_weightsdir_to_weightsfile weightsdir_to_weightsfile.cpp Weights.cpp)
    target_include_directories(adams2019_weightsdir_to_weightsfile PRIVATE ${COMMON_DIR})
    target_link_libraries(adams2019_weightsdir_to_weightsfile PRIVATE Halide::Runtime)
endif ()
