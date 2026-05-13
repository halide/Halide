##
# Tests for the Adams2019 autoscheduler
##

# =================================================================

# demo_generator.cpp
add_halide_generator(adams2019_demo.generator
                     SOURCES demo_generator.cpp)

add_halide_library(adams2019_demo FROM adams2019_demo.generator
                   GENERATOR demo
                   TARGETS cmake
                   AUTOSCHEDULER Halide::Adams2019
                   REGISTRATION DEMO_REGISTRATION_FILE)

add_executable(adams2019_demo_apps_autoscheduler ${DEMO_REGISTRATION_FILE})
target_link_libraries(adams2019_demo_apps_autoscheduler PRIVATE adams2019_demo Halide::RunGenMain)

add_halide_test(adams2019_demo_apps_autoscheduler
                COMMAND adams2019_demo_apps_autoscheduler --benchmarks=all --benchmark_min_time=1 --estimate_all --success
                GROUPS adams2019 autoschedulers_cpu multithreaded)

# =================================================================

# included_schedule_file_generator.cpp
add_halide_generator(adams2019_included_schedule_file.generator
                     SOURCES included_schedule_file_generator.cpp)

add_halide_library(adams2019_included_schedule_file FROM adams2019_included_schedule_file.generator
                   GENERATOR included_schedule_file
                   TARGETS cmake
                   AUTOSCHEDULER Halide::Adams2019
                   REGISTRATION adams2019_included_schedule_reg)

add_executable(adams2019_demo_included_schedule_file ${adams2019_included_schedule_reg})
target_link_libraries(adams2019_demo_included_schedule_file PRIVATE adams2019_included_schedule_file Halide::RunGenMain)

add_halide_test(adams2019_demo_included_schedule_file
                COMMAND adams2019_demo_included_schedule_file --benchmarks=all --benchmark_min_time=1 --estimate_all --success
                GROUPS adams2019 autoschedulers_cpu multithreaded)

# =================================================================
# Smaller tests

# test.cpp
add_executable(adams2019_test_apps_autoscheduler test.cpp)
target_link_libraries(adams2019_test_apps_autoscheduler PRIVATE Halide::Halide Halide::Tools ${CMAKE_DL_LIBS})
add_dependencies(adams2019_test_apps_autoscheduler Halide_Adams2019)

add_halide_test(adams2019_test_apps_autoscheduler
                COMMAND adams2019_test_apps_autoscheduler $<TARGET_FILE:Halide_Adams2019> $<TARGET_FILE_DIR:Halide_Adams2019>/baseline.weights
                GROUPS adams2019 autoschedulers_cpu multithreaded)
set_tests_properties(adams2019_test_apps_autoscheduler PROPERTIES
                     ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:Halide_Adams2019>")

# test_function_dag
add_executable(adams2019_test_function_dag
               test_function_dag.cpp
               ${Halide_SOURCE_DIR}/src/autoschedulers/adams2019/FunctionDAG.cpp)
target_include_directories(adams2019_test_function_dag PRIVATE
                           ${Halide_SOURCE_DIR}/src/autoschedulers/adams2019)
target_link_libraries(adams2019_test_function_dag PRIVATE Halide::Plugin)

add_halide_test(adams2019_test_function_dag
                GROUPS adams2019 autoschedulers_cpu)
