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

# cost_model, train_cost_model
set(COST_MODEL_GENERATOR_FILES cost_model_generator.cpp)
halide_generator(cost_model.generator SRCS ${COST_MODEL_GENERATOR_FILES})
halide_generator(train_cost_model.generator SRCS ${COST_MODEL_GENERATOR_FILES})
halide_library_from_generator(cost_model
                              FUNCTION_NAME cost_model
                              GENERATOR cost_model.generator)
halide_library_from_generator(train_cost_model
                              FUNCTION_NAME train_cost_model
                              GENERATOR train_cost_model.generator)

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
target_include_directories(retrain_cost_model_process
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../support)
target_link_libraries(retrain_cost_model_process
                      PRIVATE cost_model train_cost_model)

# =======================================================

# libauto_schedule
add_library(auto_schedule
            SHARED
            ASLog.cpp
            AutoSchedule.cpp
            DefaultCostModel.cpp
            FunctionDAG.cpp
            LoopNest.cpp
            Weights.cpp
            ${WF_CPP})

if(HALIDE_LIBRARY_TYPE STREQUAL STATIC)
  target_link_libraries(auto_schedule PRIVATE cost_model train_cost_model)
else()
  target_link_libraries(auto_schedule
                        PRIVATE cost_model train_cost_model Halide)
endif()

if(NOT MSVC)
  set_target_properties(auto_schedule PROPERTIES LINK_FLAGS "-rdynamic")
elseif(APPLE)
  set_target_properties(auto_schedule
                        PROPERTIES LINK_FLAGS "-undefined dynamic_lookup")
endif()

if(NOT HALIDE_ENABLE_RTTI)
  if(NOT MSVC)
    target_compile_options(auto_schedule PUBLIC "-fno-rtti")
  else()
    target_compile_options(auto_schedule PUBLIC "/GR-")
  endif()
endif()

install(TARGETS auto_schedule LIBRARY DESTINATION bin)

# =======================================================

# demo_apps_autoscheduler
halide_library(demo
               SRCS demo_generator.cpp
               FUNCTION_NAME demo
               GENERATOR_ARGS auto_schedule=true -p $<TARGET_FILE:auto_schedule> -s Adams2019)
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

add_custom_command(TARGET demo_apps_autoscheduler POST_BUILD
                   COMMAND demo_apps_autoscheduler
                           --benchmarks=all
                           --benchmark_min_time=1
                           --estimate_all
                   WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# =======================================================

# demo_included_schedule_file
halide_library(included_schedule_file
               SRCS included_schedule_file_generator.cpp
               FUNCTION_NAME included_schedule_file
               GENERATOR_ARGS auto_schedule=true -p $<TARGET_FILE:auto_schedule> -s Adams2019)
add_dependencies(included_schedule_file auto_schedule)

_halide_genfiles_dir(included_schedule_file GENFILES_DIR)
set(INCLUDED_SCHEDULE_FILE_REGISTRATION_FILE
    ${GENFILES_DIR}/included_schedule_file.registration.cpp)

add_executable(demo_included_schedule_file ${HALIDE_TOOLS_DIR}/RunGenMain.cpp
               ${INCLUDED_SCHEDULE_FILE_REGISTRATION_FILE})
target_include_directories(
  demo_included_schedule_file
  PRIVATE "${HALIDE_INCLUDE_DIR}" "${HALIDE_TOOLS_DIR}")
halide_use_image_io(demo_included_schedule_file)
target_link_libraries(demo_included_schedule_file
                      PRIVATE included_schedule_file)
add_dependencies(demo_included_schedule_file included_schedule_file
                 auto_schedule)

add_custom_command(TARGET demo_included_schedule_file POST_BUILD
                   COMMAND demo_included_schedule_file
                           --benchmarks=all
                           --benchmark_min_time=1
                           --estimate_all
                   WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# =======================================================

add_executable(featurization_to_sample featurization_to_sample.cpp)

add_executable(get_host_target get_host_target.cpp)
target_include_directories(get_host_target PRIVATE "${HALIDE_INCLUDE_DIR}")
target_link_libraries(get_host_target PRIVATE Halide)

add_executable(test_apps_autoscheduler test.cpp)
target_include_directories(
  test_apps_autoscheduler
  PRIVATE "${HALIDE_INCLUDE_DIR}" "${HALIDE_TOOLS_DIR}")
target_link_libraries(test_apps_autoscheduler PRIVATE Halide)
if (NOT WIN32)
  target_link_libraries(test_apps_autoscheduler PRIVATE dl)
endif()

add_executable(test_perfect_hash_map test_perfect_hash_map.cpp)

add_executable(test_function_dag test_function_dag.cpp FunctionDAG.h FunctionDAG.cpp ASLog.h ASLog.cpp)
target_include_directories(
  test_function_dag
  PRIVATE "${HALIDE_INCLUDE_DIR}" "${HALIDE_TOOLS_DIR}")
target_link_libraries(test_function_dag PRIVATE Halide)
#if (NOT WIN32)
#  target_link_libraries(test_function_dag PRIVATE dl)
#endif()

add_executable(weightsdir_to_weightsfile weightsdir_to_weightsfile.cpp
               Weights.cpp)
