add_subdirectory(src/halide/runtime)

if (WITH_TEST_PYTHON)
    if (Halide_TARGET MATCHES "wasm")
        message(WARNING "Python runtime tests are skipped under WASM.")
    else ()
        add_subdirectory(test)
    endif ()
endif ()
