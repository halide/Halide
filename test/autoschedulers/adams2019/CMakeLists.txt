if (NOT TARGET Halide::Adams2019)
    message(STATUS "Disabling adams2019 tests for static Halide")
    return()
endif ()

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
    list(APPEND ARGS_LABELS adams2019)
    list(APPEND ARGS_LABELS autoschedulers_cpu)

    add_test(NAME ${NAME}
             COMMAND ${ARGS_COMMAND})
    set_tests_properties(${NAME}
                         PROPERTIES
                         LABELS "${ARGS_LABELS}"
                         ENVIRONMENT "${ENVIRONMENT}")
endfunction()


##
# Tests and demos
# TODO(#4053): move these to a separate folder since they're tests.
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

add_adams2019_test(adams2019_demo_apps_autoscheduler
                   COMMAND adams2019_demo_apps_autoscheduler --benchmarks=all --benchmark_min_time=1 --estimate_all)

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

add_adams2019_test(adams2019_demo_included_schedule_file
                   COMMAND adams2019_demo_included_schedule_file --benchmarks=all --benchmark_min_time=1 --estimate_all)

# =================================================================
# Smaller tests

# test.cpp
add_executable(adams2019_test_apps_autoscheduler test.cpp)
target_link_libraries(adams2019_test_apps_autoscheduler PRIVATE Halide::Halide Halide::Tools ${CMAKE_DL_LIBS})
add_dependencies(adams2019_test_apps_autoscheduler Halide_Adams2019)

add_adams2019_test(adams2019_test_apps_autoscheduler
                   COMMAND adams2019_test_apps_autoscheduler $<TARGET_FILE:Halide_Adams2019> $<TARGET_FILE_DIR:Halide_Adams2019>/baseline.weights
                   LABELS multithreaded
                   ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:Halide_Adams2019>:$ENV{LD_LIBRARY_PATH}")

