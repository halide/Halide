add_library(gradient_autoscheduler
            SHARED
            GradientAutoscheduler.cpp
            ASLog.cpp)
target_link_libraries(gradient_autoscheduler PRIVATE "${HALIDE_COMPILER_LIB}")
target_include_directories(gradient_autoscheduler PRIVATE "${HALIDE_INCLUDE_DIR}" "${HALIDE_TOOLS_DIR}")

add_executable(gradient_autoscheduler_test_cpp test.cpp)
target_link_libraries(gradient_autoscheduler_test_cpp PRIVATE gradient_autoscheduler "${HALIDE_COMPILER_LIB}")
target_include_directories(gradient_autoscheduler_test_cpp PRIVATE "${HALIDE_INCLUDE_DIR}" "${HALIDE_TOOLS_DIR}")

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
             PROPERTY ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")

#TODO: run_test_py: halide python binding is missing CMakeLists.txt

add_test(NAME test_gradient_autoscheduler_generator
         COMMAND gradient_autoscheduler_demo.rungen --benchmarks=all --benchmark_min_time=1 --estimate_all)
