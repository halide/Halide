set(scripts
    bilateral_grid.py
    blur.py
    erode.py
    interpolate.py
    local_laplacian.py)

set(PYTHONPATH
    "$<TARGET_FILE_DIR:Halide::Python>"
)

set(TEST_ENV
    "PYTHONPATH=${PYTHONPATH}"
    "HL_TARGET=${Halide_TARGET}"
    "TEST_TMPDIR=$<SHELL_PATH:${CMAKE_CURRENT_BINARY_DIR}>"
    "TEST_IMAGES_DIR=$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/../../../apps/images>"
)

foreach (script IN LISTS scripts)
    cmake_path(GET script STEM base)
    add_test(NAME python_apps_${base}
             COMMAND Python3::Interpreter "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/${script}>")
    set_tests_properties(
        python_apps_${base} PROPERTIES
        LABELS python
        ENVIRONMENT "${TEST_ENV}"
        ENVIRONMENT_MODIFICATION "${PYTHONPATH}"
    )
endforeach ()
