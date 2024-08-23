if (EXISTS "${NODE_JS_EXECUTABLE}")
    message(DEPRECATION "NODE_JS_EXECUTABLE has been renamed to NodeJS_EXECUTABLE")
    set(NodeJS_EXECUTABLE "${NODE_JS_EXECUTABLE}")
    set(NodeJS_EXECUTABLE "${NODE_JS_EXECUTABLE}" CACHE PATH "")
endif ()

find_program(
    NodeJS_EXECUTABLE
    NAMES node nodejs
)

set(NodeJS_VERSION "")
if (NodeJS_EXECUTABLE)
    execute_process(
        COMMAND "${NodeJS_EXECUTABLE}" --version
        OUTPUT_VARIABLE NodeJS_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string(REPLACE "v" "" NodeJS_VERSION "${NodeJS_VERSION}")
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    NodeJS
    REQUIRED_VARS NodeJS_EXECUTABLE
    VERSION_VAR NodeJS_VERSION
    HANDLE_COMPONENTS
)

if (NodeJS_FOUND AND NOT TARGET NodeJS::node)
    add_executable(NodeJS::node IMPORTED)
    set_target_properties(
        NodeJS::node PROPERTIES IMPORTED_LOCATION "${NodeJS_EXECUTABLE}"
    )
endif ()
