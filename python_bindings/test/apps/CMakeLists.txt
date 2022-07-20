set(SCRIPTS
    bilateral_grid.py
    blur.py
    erode.py
    interpolate.py
    local_laplacian.py)

make_shell_path(
    PYTHONPATH
    "$<TARGET_FILE_DIR:Halide::Python>"
    "${Halide_SOURCE_DIR}/python_bindings/src"
)

set(TEST_ENV
    "PYTHONPATH=${PYTHONPATH}"
    "HL_TARGET=${Halide_TARGET}"
    "TEST_TMPDIR=$<SHELL_PATH:${CMAKE_CURRENT_BINARY_DIR}>"
    "TEST_IMAGES_DIR=$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/../../../apps/images>"
)

foreach (SCRIPT IN LISTS SCRIPTS)
    get_filename_component(BASE ${SCRIPT} NAME_WE)
    add_test(NAME python_apps_${BASE}
             COMMAND Python3::Interpreter "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/${SCRIPT}>")
    set_tests_properties(python_apps_${BASE} PROPERTIES
                         LABELS python
                         ENVIRONMENT "${TEST_ENV}")
endforeach ()
