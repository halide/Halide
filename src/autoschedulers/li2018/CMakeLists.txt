add_autoscheduler(NAME Li2018 SOURCES GradientAutoscheduler.cpp)

# ==========================================================
# TODO(#4053): move these to a separate folder since they're tests.

add_executable(demo_gradient.generator demo_generator.cpp)
target_link_libraries(demo_gradient.generator PRIVATE Halide::Generator)

add_halide_library(demo_gradient FROM demo_gradient.generator
                   TARGETS cmake
                   GENERATOR demo
                   FUNCTION_NAME demo
                   AUTOSCHEDULER Halide::Li2018
                   REGISTRATION DEMO_REGISTRATION_FILE)

add_executable(demo_gradient_autoscheduler ${DEMO_REGISTRATION_FILE})
target_link_libraries(demo_gradient_autoscheduler PRIVATE demo_gradient Halide::RunGenMain)

add_test(NAME demo_gradient_autoscheduler
         COMMAND demo_gradient_autoscheduler --benchmarks=all --benchmark_min_time=1 --estimate_all)

set_tests_properties(demo_gradient_autoscheduler PROPERTIES LABELS Li2018)

##

if (BUILD_SHARED_LIBS)
    add_executable(gradient_autoscheduler_test_cpp test.cpp)
    target_link_libraries(gradient_autoscheduler_test_cpp PRIVATE Halide::Halide)

    add_test(NAME gradient_autoscheduler_test_cpp
             COMMAND gradient_autoscheduler_test_cpp $<TARGET_FILE:Halide_Li2018>)

    set_tests_properties(gradient_autoscheduler_test_cpp PROPERTIES LABELS Li2018)
endif ()

##

if (WITH_PYTHON_BINDINGS)
    # TODO(#4053): rework this as an app under python_bindings.
    # TODO(#4876): Disabled due to issue #4876
    if (FALSE)
        find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

        add_test(NAME gradient_autoscheduler_test_py
                 COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/test.py")

        set(PYTHONPATH "$<SHELL_PATH:$<TARGET_FILE_DIR:Halide::Python>>")

        if (WIN32)
            set(SEP "\\$<SEMICOLON>")
        else ()
            set(SEP ":")
        endif ()

        set(_PATH "$<SHELL_PATH:$<TARGET_FILE_DIR:Halide_Li2018>>;$<SHELL_PATH:$<TARGET_FILE_DIR:Halide::Halide>>;$ENV{PATH}")
        string(REPLACE ";" "${SEP}" _PATH "${_PATH}")
        set_tests_properties(gradient_autoscheduler_test_py PROPERTIES
                             LABELS Li2018
                             ENVIRONMENT "PYTHONPATH=${PYTHONPATH};PATH=${_PATH}")
    endif ()
endif ()
