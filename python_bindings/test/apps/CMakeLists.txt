set(tests
    bilateral_grid_shell.py
    blur.py
    erode.py
    interpolate.py
    local_laplacian.py)

set(TEST_TMPDIR "$<SHELL_PATH:${CMAKE_CURRENT_BINARY_DIR}>")
set(TEST_IMAGES_DIR "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/../../../apps/images>")

set(DEPS_bilateral_grid_shell   py_aot_bilateral_grid)
set(PYPATH_bilateral_grid_shell "$<TARGET_FILE_DIR:py_aot_bilateral_grid>")
set(ARGS_bilateral_grid_shell   ${TEST_IMAGES_DIR}/gray.png ${TEST_TMPDIR}/out.png 0.1 10)

foreach (test IN LISTS tests)
    cmake_path(GET test STEM test_name)
    add_python_test(
        FILE "${test}"
        TEST_ARGS ${ARGS_${test_name}}
        LABEL python_apps
        DEPENDS ${DEPS_${test_name}}
        PYTHONPATH ${PYPATH_${test_name}}
        ENVIRONMENT
        "TEST_TMPDIR=${TEST_TMPDIR}"
        "TEST_IMAGES_DIR=${TEST_IMAGES_DIR}"
    )
endforeach ()
