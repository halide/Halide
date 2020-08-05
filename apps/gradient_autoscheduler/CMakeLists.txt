# ================================================================================
# Halide autoscheduler plugins rely on weak linking to work with static libraries.
# This is not standard C++ and only works on Linux / macOS. Nothing special needs
# to be done when linking to a shared version of Halide, however.

if (NOT BUILD_SHARED_LIBS)
    if (MSVC)
        message(FATAL_ERROR "Autoscheduler plugins cannot be built against static Halide on Windows")
    endif ()

    # Need to enable exports for the plugins to find Halide's symbols.
    set(CMAKE_ENABLE_EXPORTS ON)
endif ()

# =================================
# Define the autoscheduler library.

add_library(Halide_Li2018 MODULE GradientAutoscheduler.cpp ASLog.cpp)
add_library(Halide::Li2018 ALIAS Halide_Li2018)
set_target_properties(Halide_Li2018 PROPERTIES
                      EXPORT_NAME Li2018
                      OUTPUT_NAME gradient_autoscheduler)

target_link_libraries(Halide_Li2018 PRIVATE Halide::Plugin)

if (NOT HALIDE_ENABLE_RTTI)
    target_compile_options(Halide_Li2018 PRIVATE
                           $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/GR->
                           $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-fno-rtti>)
endif ()

# ==========================================================
# TODO(#4053): move these to a separate folder since they're tests.

add_executable(demo_gradient.generator demo_generator.cpp)
target_link_libraries(demo_gradient.generator PRIVATE Halide::Generator)

add_halide_library(demo_gradient FROM demo_gradient.generator
                   GENERATOR demo
                   FUNCTION_NAME demo
                   PARAMS auto_schedule=true
                   AUTOSCHEDULER Halide::Li2018
                   REGISTRATION DEMO_REGISTRATION_FILE)

add_executable(demo_gradient_autoscheduler ${DEMO_REGISTRATION_FILE})
target_link_libraries(demo_gradient_autoscheduler PRIVATE demo_gradient Halide::RunGenMain)

add_test(NAME demo_gradient_autoscheduler
         COMMAND demo_gradient_autoscheduler --benchmarks=all --benchmark_min_time=1 --estimate_all)

set_tests_properties(demo_gradient_autoscheduler PROPERTIES LABELS Li2018)

##

# Hack to include all symbols in the gradient autoscheduler test.
# DerivativeUtils.h/inference_bounds is not used elsewhere and so doesn't get linked to the test.
unset(WHOLE_ARCHIVE)
unset(NO_WHOLE_ARCHIVE)
if (NOT BUILD_SHARED_LIBS)
    set(WHOLE_ARCHIVE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wl,-whole-archive> $<$<CXX_COMPILER_ID:AppleClang>:-Wl,-all_load>)
    set(NO_WHOLE_ARCHIVE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wl,-no-whole-archive> $<$<CXX_COMPILER_ID:AppleClang>:-Wl,-noall_load>)
endif ()

add_executable(gradient_autoscheduler_test_cpp test.cpp)
target_link_libraries(gradient_autoscheduler_test_cpp PRIVATE ${WHOLE_ARCHIVE} Halide::Halide ${NO_WHOLE_ARCHIVE})

add_test(NAME gradient_autoscheduler_test_cpp
         COMMAND gradient_autoscheduler_test_cpp
         WORKING_DIRECTORY $<TARGET_FILE_DIR:Halide_Li2018>)

set_tests_properties(gradient_autoscheduler_test_cpp PROPERTIES
                     LABELS Li2018
                     ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:Halide_Li2018>")

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
