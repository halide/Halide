add_library(the_sort_function MODULE the_sort_function.c)
target_link_libraries(the_sort_function PRIVATE Halide::Runtime)

set(tests
    addconstant_test.py
    atomics.py
    autodiff.py
    basics.py
    bit_test.py
    boundary_conditions.py
    buffer.py
    callable.py
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
set(
    PYTHONPATH
    "$<TARGET_FILE_DIR:py_aot_bit>"
    "$<TARGET_FILE_DIR:py_stub_bit>"
    "$<TARGET_FILE_DIR:Halide::Python>"
    "${Halide_SOURCE_DIR}/python_bindings/src"
)
list(TRANSFORM PYTHONPATH PREPEND "PYTHONPATH=path_list_prepend:")

foreach (test IN LISTS tests)
    cmake_path(GET test STEM test_name)
    add_test(NAME python_correctness_${test_name}
             COMMAND Python3::Interpreter "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/${test}>")
    set_tests_properties(
        python_correctness_${test_name} PROPERTIES
        LABELS "python"
        ENVIRONMENT "HL_TARGET=${Halide_TARGET}"
        ENVIRONMENT_MODIFICATION "${PYTHONPATH}"
    )
endforeach ()
