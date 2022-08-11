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

set(PY_GENERATORS "${CMAKE_CURRENT_SOURCE_DIR}/../generators")

set(PYPATH_addconstant_test    "$<TARGET_FILE_DIR:py_aot_addconstant>")
set(PYPATH_bit_test            "$<TARGET_FILE_DIR:py_aot_bit>")
set(PYPATH_callable            "$<TARGET_FILE_DIR:py_stub_simple>;${PY_GENERATORS}")
set(PYPATH_pystub              "$<TARGET_FILE_DIR:py_stub_bit>;${PY_GENERATORS}")
set(PYPATH_user_context_test   "$<TARGET_FILE_DIR:py_aot_user_context>;${PY_GENERATORS}")

foreach (test IN LISTS tests)
    cmake_path(GET test STEM test_name)
    add_python_test(
        FILE "${test}"
        LABEL python_correctness
        # Ensure that everything in generators/ is built before us.
        # This is dramatic overkill but simplifies our build setup considerably.
        DEPENDS ${PYTHON_CORRECTNESS_GENERATOR_DEPS}
        PYTHONPATH "${PYPATH_${test_name}}"
    )
endforeach ()

# "$<TARGET_FILE_DIR:py_aot_bit>" "$<TARGET_FILE_DIR:py_stub_bit>"