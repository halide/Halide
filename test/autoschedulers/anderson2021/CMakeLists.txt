if (NOT TARGET Halide::Anderson2021)
    message(STATUS "Disabling anderson2021 tests for static Halide")
    return()
endif ()

##
# Build rules for the Anderson2021 autoscheduler library
##

function(add_anderson2021_test NAME)
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
    list(APPEND ARGS_LABELS anderson2021)
    list(APPEND ARGS_LABELS autoschedulers_cuda)
    # No: anderson2021 didn't have this label before, and it's going away soon, don't add it now
    # list(APPEND ARGS_LABELS auto_schedule)

    add_test(NAME ${NAME}
             COMMAND ${ARGS_COMMAND})
    set_tests_properties(${NAME}
                         PROPERTIES
                         LABELS "${ARGS_LABELS}"
                         ENVIRONMENT "${ENVIRONMENT}")
endfunction()


##
# Tests and demos
##

# =================================================================

add_halide_generator(anderson2021_demo.generator
                     SOURCES demo_generator.cpp)

add_halide_library(anderson2021_demo FROM anderson2021_demo.generator
                   GENERATOR demo
                   TARGETS cmake
                   FEATURES cuda cuda_capability_70
                   AUTOSCHEDULER Halide::Anderson2021
                   REGISTRATION DEMO_REGISTRATION_FILE)

add_executable(anderson2021_demo_apps_autoscheduler ${DEMO_REGISTRATION_FILE})
target_link_libraries(anderson2021_demo_apps_autoscheduler PRIVATE anderson2021_demo Halide::RunGenMain)

add_anderson2021_test(anderson2021_demo_apps_autoscheduler
                      COMMAND anderson2021_demo_apps_autoscheduler --benchmarks=all --benchmark_min_time=1 --estimate_all --skip_bad_environment)

# =================================================================

add_halide_generator(anderson2021_included_schedule_file.generator
                     SOURCES included_schedule_file_generator.cpp)

add_halide_library(anderson2021_included_schedule_file FROM anderson2021_included_schedule_file.generator
                   GENERATOR included_schedule_file
                   TARGETS cmake
                   FEATURES cuda cuda_capability_70
                   AUTOSCHEDULER Halide::Anderson2021
                   REGISTRATION anderson2021_included_schedule_reg)

add_executable(anderson2021_demo_included_schedule_file ${anderson2021_included_schedule_reg})
target_link_libraries(anderson2021_demo_included_schedule_file PRIVATE anderson2021_included_schedule_file Halide::RunGenMain)

add_anderson2021_test(anderson2021_demo_included_schedule_file
                   COMMAND anderson2021_demo_included_schedule_file --benchmarks=all --benchmark_min_time=1 --estimate_all --skip_bad_environment)

# =================================================================
# Smaller tests

add_executable(anderson2021_test_apps_autoscheduler test.cpp)
target_link_libraries(anderson2021_test_apps_autoscheduler PRIVATE Halide::Halide Halide::Tools ${CMAKE_DL_LIBS})
add_dependencies(anderson2021_test_apps_autoscheduler Halide_Anderson2021)

add_anderson2021_test(anderson2021_test_apps_autoscheduler
                   COMMAND anderson2021_test_apps_autoscheduler $<TARGET_FILE:Halide_Anderson2021>
                   LABELS multithreaded
                   ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:Halide_Anderson2021>:$ENV{LD_LIBRARY_PATH}")

