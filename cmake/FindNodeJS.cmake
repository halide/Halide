find_program(NODE_JS_EXECUTABLE
             NAMES nodejs node)

if (NODE_JS_EXECUTABLE)
    execute_process(COMMAND "${NODE_JS_EXECUTABLE}" --version
                    OUTPUT_VARIABLE NODE_JS_VERSION_RAW
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NODE_JS_VERSION_RAW MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)$")
        set(NODE_JS_VERSION "${CMAKE_MATCH_1}")
    else ()
        # Failed to get version; mark as not-found
        unset(NODE_JS_EXECUTABLE CACHE)
        set(NODE_JS_EXECUTABLE "")
        set(NODE_JS_VERSION "")
    endif ()
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    NodeJS
    HANDLE_COMPONENTS
    REQUIRED_VARS NODE_JS_EXECUTABLE
    VERSION_VAR NODE_JS_VERSION
)

if (NodeJS_FOUND AND NOT TARGET NodeJS::Interpreter)
    add_executable(NodeJS::Interpreter IMPORTED)
    set_target_properties(NodeJS::Interpreter PROPERTIES IMPORTED_LOCATION "${NODE_JS_EXECUTABLE}")
endif ()
