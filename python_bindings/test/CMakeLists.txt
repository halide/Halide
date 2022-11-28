if (TARGET_WEBASSEMBLY AND Halide_TARGET MATCHES "wasm")
    message(WARNING "Python tests are skipped under WASM.")
    return()
endif ()

add_subdirectory(correctness)
add_subdirectory(generators)
