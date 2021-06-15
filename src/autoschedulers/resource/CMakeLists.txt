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

add_halide_library(cost_model FROM cost_model.generator
                   TARGETS cmake)
add_halide_library(train_cost_model FROM cost_model.generator
                   TARGETS cmake
                   USE_RUNTIME cost_model.runtime)

# retrain_cost_model
add_executable(retrain_cost_model
               ASLog.cpp
               DefaultCostModel.cpp
               Weights.cpp
               retrain_cost_model.cpp
               ${WF_CPP})
target_link_libraries(retrain_cost_model PRIVATE cost_model train_cost_model Halide::Halide Halide::Plugin)
