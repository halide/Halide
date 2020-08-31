# Ensure that plugins export only what is needed to load them.
# Everything else should be omitted to keep binary size low.
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)

if (TARGET_WEBASSEMBLY AND Halide_TARGET MATCHES "wasm")
    # Don't attempt to build these for wasm yet.
else ()
    add_subdirectory(autoschedulers)
endif ()
