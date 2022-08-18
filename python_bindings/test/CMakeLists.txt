if (TARGET_WEBASSEMBLY AND Halide_TARGET MATCHES "wasm")
    message(WARNING "Python tests are skipped under WASM.")
    return()
endif ()

# generators must be added first, as the build rules in apps/
# and correctness/ depend on it
add_subdirectory(generators)
add_subdirectory(apps)
add_subdirectory(correctness)
