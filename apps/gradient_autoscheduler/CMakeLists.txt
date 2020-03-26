# Note: must use MODULE here (not SHARED) to get .so (instead of .dylib) on OSX.
# This means that this can only be opened dynamically (not linked directly), but that's ok.
add_library(gradient_autoscheduler
            MODULE
            GradientAutoscheduler.cpp
            ASLog.cpp)
target_link_libraries(gradient_autoscheduler PRIVATE "${HALIDE_COMPILER_LIB}")
target_include_directories(gradient_autoscheduler PRIVATE "${HALIDE_INCLUDE_DIR}" "${HALIDE_TOOLS_DIR}")

if(NOT MSVC)
  set_target_properties(gradient_autoscheduler PROPERTIES LINK_FLAGS "-rdynamic")
elseif(APPLE)
  set_target_properties(gradient_autoscheduler PROPERTIES LINK_FLAGS "-undefined dynamic_lookup")
endif()

if(NOT HALIDE_ENABLE_RTTI)
  if(NOT MSVC)
    target_compile_options(gradient_autoscheduler PRIVATE "-fno-rtti")
  else()
    target_compile_options(gradient_autoscheduler PRIVATE "/GR-")
  endif()
endif()

install(TARGETS gradient_autoscheduler LIBRARY DESTINATION bin)

add_executable(gradient_autoscheduler_test_cpp test.cpp)
target_link_libraries(gradient_autoscheduler_test_cpp PRIVATE Halide)
target_include_directories(
    gradient_autoscheduler_test_cpp
    PRIVATE "${HALIDE_INCLUDE_DIR}" "${HALIDE_TOOLS_DIR}")

halide_library(gradient_autoscheduler_demo
               SRCS demo_generator.cpp
               GENERATOR_NAME demo
               GENERATOR_ARGS auto_schedule=true -p $<TARGET_FILE:gradient_autoscheduler> -s Li2018)
add_dependencies(gradient_autoscheduler_demo gradient_autoscheduler)
add_dependencies(gradient_autoscheduler_demo.rungen gradient_autoscheduler)
set_target_properties(gradient_autoscheduler_demo.rungen PROPERTIES EXCLUDE_FROM_ALL FALSE)

enable_testing()
add_test(NAME run_gradient_autoscheduler_test_cpp
         COMMAND gradient_autoscheduler_test_cpp)
set_property(TEST run_gradient_autoscheduler_test_cpp
             PROPERTY ENVIRONMENT
                      "$<$<NOT:$<PLATFORM_ID:Windows>>:LD_LIBRARY_PATH=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}>"
                      "$<$<PLATFORM_ID:Windows>:PATH=$<TARGET_FILE_DIR:${HALIDE_COMPILER_LIB}>\;$<TARGET_FILE_DIR:gradient_autoscheduler>>")

if (WITH_PYTHON_BINDINGS)
  add_test(NAME run_gradient_autoscheduler_test_py
           COMMAND "${PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/test.py")
  set_property(TEST run_gradient_autoscheduler_test_py
               PROPERTY ENVIRONMENT
                        "PYTHONPATH=$<TARGET_FILE_DIR:halide_py>"
                        "$<$<NOT:$<PLATFORM_ID:Windows>>:LD_LIBRARY_PATH=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}>"
                        "$<$<PLATFORM_ID:Windows>:PATH=$<TARGET_FILE_DIR:${HALIDE_COMPILER_LIB}>\;$<TARGET_FILE_DIR:gradient_autoscheduler>>")
endif()

add_test(NAME test_gradient_autoscheduler_generator
         COMMAND gradient_autoscheduler_demo.rungen --benchmarks=all --benchmark_min_time=1 --estimate_all)
