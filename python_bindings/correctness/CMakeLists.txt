add_subdirectory(generators)

add_library(the_sort_function MODULE the_sort_function.c)
target_link_libraries(the_sort_function PRIVATE Halide::Runtime)

set(TESTS
    addconstant_test.py
    atomics.py
    autodiff.py
    basics.py
    bit_test.py
    boundary_conditions.py
    buffer.py
    compile_to.py
    division.py
    extern.py
    float_precision_test.py
    iroperator.py
    multipass_constraints.py
    pystub.py
    rdom.py
    realize_warnings.py
    target.py
    tuple_select.py
    type.py
    user_context_test.py
    var.py
    )

# Use generator expressions to get the true output paths of these files.
make_shell_path(PYTHONPATH
                "$<TARGET_FILE_DIR:py_aot_bit>"
                "$<TARGET_FILE_DIR:py_stub_bit>"
                "$<TARGET_FILE_DIR:Halide::Python>"
                )

foreach (TEST IN LISTS TESTS)
    get_filename_component(TEST_NAME ${TEST} NAME_WE)
    add_test(NAME python_correctness_${TEST_NAME}
             COMMAND Python3::Interpreter "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/${TEST}>")
    set_tests_properties(python_correctness_${TEST_NAME} PROPERTIES
                         LABELS "python"
                         ENVIRONMENT "PYTHONPATH=${PYTHONPATH};HL_TARGET=${Halide_TARGET}")
endforeach ()
