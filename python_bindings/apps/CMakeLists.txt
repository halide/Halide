if (TARGET_WEBASSEMBLY AND Halide_TARGET MATCHES "wasm")
    message(WARNING "Python apps are skipped under WASM.")
    return()
endif ()

set(TEST_TMPDIR "$<SHELL_PATH:${CMAKE_CURRENT_BINARY_DIR}>")
set(TEST_IMAGES_DIR "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/../../apps/images>")

set(APPS
    bilateral_grid
    blur
    interpolate
    local_laplacian)

set(GENERATORS_bilateral_grid   bilateral_grid bilateral_grid_Adams2019 bilateral_grid_Li2018 bilateral_grid_Mullapudi2016)
set(GENERATORS_interpolate      interpolate interpolate_Mullapudi2016)
set(GENERATORS_local_laplacian  local_laplacian local_laplacian_Mullapudi2016)
set(GENERATORS_blur             blur)

set(ARGS_bilateral_grid   ${TEST_IMAGES_DIR}/gray.png 0.1 ${TEST_TMPDIR}/out.png)
set(ARGS_blur             ${TEST_IMAGES_DIR}/gray.png ${TEST_TMPDIR}/out.png)
set(ARGS_interpolate      ${TEST_IMAGES_DIR}/rgba.png ${TEST_TMPDIR}/out.png)
set(ARGS_local_laplacian  ${TEST_IMAGES_DIR}/rgba.png 8 1 1 ${TEST_TMPDIR}/out.png)

foreach (app IN LISTS APPS)
    set(app_generator_src "${app}_generator.py")
    add_halide_generator(app_gen_${app}
                         SOURCES ${app_generator_src})

    set(DEPS "")
    foreach (G IN ITEMS ${GENERATORS_${app}})
        add_halide_library(app_aot_${G}
                           FROM app_gen_${app}
                           GENERATOR ${G}
                           FUNCTION_NAME ${G}
                           USE_RUNTIME ${RUNTIME_${G}}
                           PYTHON_EXTENSION _ignored_result
                           # We don't really need all the plugins at once here --
                           # it's just easier to specify them all
                           PLUGINS Halide::Adams2019 Halide::Li2018 Halide::Mullapudi2016)

        add_halide_python_extension_library(app_ext_${G}
                                            MODULE_NAME ${G}
                                            HALIDE_LIBRARIES app_aot_${G})
        list(APPEND DEPS app_ext_${G})
    endforeach()

    set(app_src "${app}_app.py")
    add_python_test(
        FILE "${app_src}"
        TEST_ARGS ${ARGS_${app}}
        LABEL python_apps
        DEPENDS ${DEPS}
        PYTHONPATH "$<TARGET_FILE_DIR:app_ext_${app}>"
        ENVIRONMENT
        "TEST_TMPDIR=${TEST_TMPDIR}"
        "TEST_IMAGES_DIR=${TEST_IMAGES_DIR}"
    )
endforeach ()
