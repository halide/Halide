# weights
file(GLOB WF baseline.weights)
get_filename_component(BASENAME_WF ${WF} NAME_WE)
set(WF_CPP ${BASENAME_WF}.cpp)

add_custom_command(OUTPUT ${WF_CPP}
                   DEPENDS ${WF} binary2cpp
                   COMMAND binary2cpp ${BASENAME_WF}_weights < ${WF} > ${WF_CPP}
                   COMMENT "${WF} -> ${WF_CPP}"
                   VERBATIM)

# =======================================================

# retrain_cost_model
add_executable(retrain_cost_model_process
               ASLog.cpp
               DefaultCostModel.cpp
               Weights.cpp
               retrain_cost_model.cpp
               ${WF_CPP})
set_target_properties(retrain_cost_model_process
                      PROPERTIES OUTPUT_NAME retrain_cost_model)

set(COST_MODEL_GENERATOR_FILES cost_model_generator.cpp)
halide_generator(cost_model.generator SRCS ${COST_MODEL_GENERATOR_FILES})
halide_generator(train_cost_model.generator SRCS ${COST_MODEL_GENERATOR_FILES})
halide_library_from_generator(cost_model
                              FUNCTION_NAME
                              cost_model
                              GENERATOR
                              cost_model.generator)
halide_library_from_generator(train_cost_model
                              FUNCTION_NAME
                              train_cost_model
                              GENERATOR
                              train_cost_model.generator)
target_link_libraries(retrain_cost_model_process
                      PRIVATE cost_model train_cost_model)
add_dependencies(retrain_cost_model_process cost_model train_cost_model)

# =======================================================

# libauto_schedule
add_library(auto_schedule
            SHARED
            ASLog.cpp
            AutoSchedule.cpp
            DefaultCostModel.cpp
            Weights.cpp
            ${WF_CPP})
target_link_libraries(auto_schedule PRIVATE cost_model train_cost_model Halide)
add_dependencies(auto_schedule cost_model train_cost_model)
install(TARGETS auto_schedule LIBRARY DESTINATION bin)

# =======================================================

# demo_apps_autoscheduler
halide_generator(demo.generator SRCS demo_generator.cpp)
halide_library_from_generator(demo
                              FUNCTION_NAME
                              demo
                              GENERATOR
                              demo.generator
                              GENERATOR_ARGS
                              auto_schedule=true
                              -p
                              $<TARGET_FILE:auto_schedule>)
add_dependencies(demo auto_schedule)

_halide_genfiles_dir(demo GENFILES_DIR)
set(DEMO_REGISTRATION_FILE ${GENFILES_DIR}/demo.registration.cpp)
add_executable(demo_apps_autoscheduler ${HALIDE_TOOLS_DIR}/RunGenMain.cpp
               ${DEMO_REGISTRATION_FILE})
target_include_directories(
  demo_apps_autoscheduler
  PRIVATE "${HALIDE_INCLUDE_DIR}" "${HALIDE_TOOLS_DIR}")
halide_use_image_io(demo_apps_autoscheduler)
target_link_libraries(demo_apps_autoscheduler PRIVATE demo)
add_dependencies(demo_apps_autoscheduler demo auto_schedule)

# =======================================================

# test_perfect_hash_map, test_apps_autoscheduler
add_executable(test_perfect_hash_map test_perfect_hash_map.cpp)

add_executable(test_apps_autoscheduler test.cpp)
target_include_directories(
  test_apps_autoscheduler
  PRIVATE "${HALIDE_INCLUDE_DIR}" "${HALIDE_TOOLS_DIR}")
target_link_libraries(test_apps_autoscheduler PRIVATE Halide)
