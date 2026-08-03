# Verify that the given shared MODULE does not depend on libHalide (the
# compiler). The standalone runtime must link only the header-only runtime
# interface plus a compiled Halide runtime, never libHalide.
#
# Invoked as: cmake -DMODULE=<path> -P check_no_libhalide.cmake

if (NOT MODULE)
    message(FATAL_ERROR "MODULE must be set")
endif ()

if (APPLE)
    execute_process(COMMAND otool -L "${MODULE}" OUTPUT_VARIABLE deps RESULT_VARIABLE rc)
else ()
    find_program(OBJDUMP objdump)
    if (OBJDUMP)
        execute_process(COMMAND "${OBJDUMP}" -p "${MODULE}" OUTPUT_VARIABLE deps RESULT_VARIABLE rc)
    else ()
        execute_process(COMMAND ldd "${MODULE}" OUTPUT_VARIABLE deps RESULT_VARIABLE rc)
    endif ()
endif ()

if (NOT rc EQUAL 0)
    message(FATAL_ERROR "Failed to inspect dependencies of ${MODULE}")
endif ()

if (deps MATCHES "libHalide")
    message(FATAL_ERROR "Runtime module ${MODULE} unexpectedly depends on libHalide:\n${deps}")
endif ()

message(STATUS "OK: ${MODULE} has no libHalide dependency")
