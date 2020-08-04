set(SCRIPTS
    bilateral_grid.py
    blur.py
    erode.py
    interpolate.py
    local_laplacian.py)

foreach (SCRIPT IN LISTS SCRIPTS)
    get_filename_component(BASE ${SCRIPT} NAME_WE)
    add_test(NAME python_apps_${BASE}
             COMMAND Python3::Interpreter "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/${SCRIPT}>")
    set_tests_properties(python_apps_${BASE} PROPERTIES
                         LABELS python
                         ENVIRONMENT "PYTHONPATH=$<SHELL_PATH:$<TARGET_FILE_DIR:Halide::Python>>;HL_TARGET=${Halide_TARGET}")
endforeach ()
