if (TARGET_WEBASSEMBLY AND Halide_TARGET MATCHES "wasm")
    # Don't attempt to build these for wasm yet.
else ()
    add_subdirectory(autoschedulers)
endif ()
