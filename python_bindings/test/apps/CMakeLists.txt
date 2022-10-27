set(tests
    bilateral_grid_app.py
    blur_app.py
    interpolate_app.py
    local_laplacian_app.py)

set(TEST_TMPDIR "$<SHELL_PATH:${CMAKE_CURRENT_BINARY_DIR}>")
set(TEST_IMAGES_DIR "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/../../../apps/images>")

set(DEPS_bilateral_grid_app   py_aot_bilateral_grid)
set(PYPATH_bilateral_grid_app "$<TARGET_FILE_DIR:py_aot_bilateral_grid>")
set(ARGS_bilateral_grid_app   ${TEST_IMAGES_DIR}/gray.png ${TEST_TMPDIR}/out.png 0.1 10)

set(DEPS_blur_app   py_aot_blur)
set(PYPATH_blur_app "$<TARGET_FILE_DIR:py_aot_blur>")
set(ARGS_blur_app   ${TEST_IMAGES_DIR}/gray.png ${TEST_TMPDIR}/out.png 10)

set(DEPS_interpolate_app   py_aot_interpolate)
set(PYPATH_interpolate_app "$<TARGET_FILE_DIR:py_aot_interpolate>")
set(ARGS_interpolate_app   ${TEST_IMAGES_DIR}/rgba.png ${TEST_TMPDIR}/out.png 10)

set(DEPS_local_laplacian_app   py_aot_local_laplacian)
set(PYPATH_local_laplacian_app "$<TARGET_FILE_DIR:py_aot_local_laplacian>")
set(ARGS_local_laplacian_app   ${TEST_IMAGES_DIR}/rgba.png 8 1 1 ${TEST_TMPDIR}/out.png 10)

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
