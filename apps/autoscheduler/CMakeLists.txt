# weights
file(GLOB WEIGHTS_DATA_FILES weights/*.data)
set(WEIGHTS_CPP_FILES)

foreach(wf ${WEIGHTS_DATA_FILES})
  get_filename_component(base_name ${wf} NAME_WE)
  set(wcpp ${base_name}.cpp)

  add_custom_command(OUTPUT ${wcpp}
                     DEPENDS ${wf} binary2cpp
                     COMMAND binary2cpp weights_${base_name} < ${wf} > ${wcpp}
                     COMMENT "${wf} -> ${wcpp}"
                     VERBATIM)
  list(APPEND WEIGHTS_CPP_FILES ${wcpp})
endforeach()

# =======================================================

# train_cost_model
add_executable(train_cost_model_process
               train_cost_model.cpp
               DefaultCostModel.cpp
               CostModel.h
               NetworkSize.h
               ${WEIGHTS_CPP_FILES})
set_target_properties(train_cost_model_process
                      PROPERTIES OUTPUT_NAME train_cost_model)

set(COST_MODEL_GENERATOR_FILES
    cost_model_generator.cpp
    cost_model_schedule.h
    NetworkSize.h)
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
target_link_libraries(train_cost_model_process
                      PRIVATE cost_model train_cost_model)
add_dependencies(train_cost_model_process cost_model train_cost_model)

# =======================================================

# libauto_schedule
add_library(libauto_schedule
            SHARED
            AutoSchedule.cpp
            DefaultCostModel.cpp
            FunctionDAG.h
            Featurization.h
            CostModel.h
            PerfectHashMap.h
            ${WEIGHTS_CPP_FILES})
target_link_libraries(libauto_schedule
                      PRIVATE cost_model train_cost_model Halide)
add_dependencies(libauto_schedule cost_model train_cost_model)
install(TARGETS libauto_schedule LIBRARY DESTINATION bin)

# =======================================================

# demo_libauto_schedule
halide_generator(demo.generator SRCS demo_generator.cpp)
halide_library_from_generator(demo
                              FUNCTION_NAME
                              demo
                              GENERATOR
                              demo.generator
                              GENERATOR_ARGS
                              auto_schedule=true
                              -p
                              $<TARGET_FILE:libauto_schedule>)
add_dependencies(demo libauto_schedule)

_halide_genfiles_dir(demo GENFILES_DIR)
set(DEMO_REGISTRATION_FILE ${GENFILES_DIR}/demo.registration.cpp)
add_executable(demo_libauto_schedule ${HALIDE_TOOLS_DIR}/RunGenMain.cpp
               ${DEMO_REGISTRATION_FILE})
target_include_directories(
  demo_libauto_schedule
  PRIVATE "${HALIDE_INCLUDE_DIR}" "${HALIDE_TOOLS_DIR}")
halide_use_image_io(demo_libauto_schedule)
target_link_libraries(demo_libauto_schedule PRIVATE demo)
add_dependencies(demo_libauto_schedule demo libauto_schedule)

# =======================================================

# augment_sample, test_perfect_hash_map, test_libauto_schedule
add_executable(augment_sample augment_sample.cpp)

add_executable(test_perfect_hash_map test_perfect_hash_map.cpp PerfectHashMap.h)

add_executable(test_libauto_schedule test.cpp)
target_include_directories(
  test_libauto_schedule
  PRIVATE "${HALIDE_INCLUDE_DIR}" "${HALIDE_TOOLS_DIR}")
target_link_libraries(test_libauto_schedule PRIVATE Halide)
