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
    multi_method_module_test.py
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

set(PY_GENERATORS "${Halide_SOURCE_DIR}/python_bindings/halide/test/generators")

set(PYPATH_callable                   "${PY_GENERATORS}")
set(PYPATH_pystub                     "${PY_GENERATORS}")

foreach (test IN LISTS tests)
    cmake_path(GET test STEM test_name)
    add_python_test(
        FILE "${test}"
        LABEL python_correctness
        # Ensure that everything in generators/ is built before us.
        # This is dramatic overkill but simplifies our build setup considerably.
        DEPENDS PythonCorrectnessGenerators
        PYTHONPATH "${PYPATH_${test_name}}"
    )
endforeach ()
