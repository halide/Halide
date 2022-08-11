set(tests
    bilateral_grid.py
    blur.py
    erode.py
    interpolate.py
    local_laplacian.py)

foreach (test IN LISTS tests)
    add_python_test(
        FILE "${test}"
        LABEL python_apps
        ENVIRONMENT
        "TEST_TMPDIR=$<SHELL_PATH:${CMAKE_CURRENT_BINARY_DIR}>"
        "TEST_IMAGES_DIR=$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/../../../apps/images>"
    )
endforeach ()
