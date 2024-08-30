##
# A helper for creating tests with correct PYTHONPATH and sanitizer preloading
##

function(add_python_test)
    cmake_parse_arguments(ARG "" "FILE;LABEL" "PYTHONPATH;ENVIRONMENT;TEST_ARGS" ${ARGN})

    list(PREPEND ARG_PYTHONPATH "$<PATH:NORMAL_PATH,$<TARGET_FILE_DIR:Halide::Python>/..>")
    list(TRANSFORM ARG_PYTHONPATH PREPEND "PYTHONPATH=path_list_prepend:")

    list(PREPEND ARG_ENVIRONMENT "HL_TARGET=${Halide_TARGET};HL_JIT_TARGET=${Halide_TARGET}")

    cmake_path(GET ARG_FILE STEM test_name)
    set(test_name "${ARG_LABEL}_${test_name}")

    add_test(
        NAME "${test_name}"
        COMMAND ${Halide_PYTHON_LAUNCHER} "$<TARGET_FILE:Python::Interpreter>" "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/${ARG_FILE}>" ${ARG_TEST_ARGS}
    )
    set_tests_properties(
        "${test_name}"
        PROPERTIES
        LABELS "python"
        ENVIRONMENT "${ARG_ENVIRONMENT}"
        ENVIRONMENT_MODIFICATION "${ARG_PYTHONPATH}"
    )
endfunction()
