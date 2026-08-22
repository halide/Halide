if (Halide_TARGET MATCHES "wasm")
    message(WARNING "Python tests are skipped under WASM.")
    return()
endif ()

if (NOT WITH_AUTOSCHEDULERS)
    message(WARNING "Python tests are skipped without autoschedulers")
    return()
endif ()

add_subdirectory(correctness)
add_subdirectory(generators)
