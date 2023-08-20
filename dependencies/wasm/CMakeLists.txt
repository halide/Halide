include(FetchContent)
include(CMakeDependentOption)

cmake_dependent_option(WITH_WABT "Include WABT Interpreter for WASM testing" ON "TARGET_WEBASSEMBLY" OFF)
cmake_dependent_option(WITH_V8 "Include V8 for WASM testing" OFF "TARGET_WEBASSEMBLY" OFF)

if (WITH_WABT AND WITH_V8)
    message(FATAL_ERROR "Cannot use both WABT and V8 at the same time, disable one of them.")
endif()

if ("${CMAKE_HOST_SYSTEM_NAME}" STREQUAL "Windows")
    if (WITH_WABT)
        message(STATUS "WITH_WABT is not yet supported on Windows")
        set(WITH_WABT OFF CACHE BOOL "WITH_WABT is not yet supported on Windows" FORCE)
    endif ()
endif ()

if (WITH_WABT)
    find_package(wabt 1.0.33 REQUIRED)
endif ()

if (WITH_V8)
    # Instructions to build V8 can be found in README_webassembly.md.
    if (NOT V8_INCLUDE_PATH)
        message(FATAL_ERROR "Please set V8_INCLUDE_PATH on the CMake command line.")
    endif()
    if (NOT V8_LIB_PATH)
        message(FATAL_ERROR "Please set V8_LIB_PATH on the CMake command line.")
    endif()

    message(STATUS "Using V8")
    add_library(Halide_V8 STATIC IMPORTED GLOBAL)
    set_target_properties(Halide_V8 PROPERTIES IMPORTED_LOCATION "${V8_LIB_PATH}")
    target_include_directories(Halide_V8 INTERFACE "${V8_INCLUDE_PATH}")
endif()

function(add_wasm_executable TARGET)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs SRCS DEPS INCLUDES OPTIONS ENABLE_IF)
    cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (args_ENABLE_IF AND NOT (${args_ENABLE_IF}))
        return()
    endif ()

    # Conceptually, we want something like this:
    # add_executable(${TARGET} ${args_SRCS})
    # if (args_INCLUDES)
    #     target_include_directories("${TARGET}" PRIVATE ${args_INCLUDES})
    # endif()
    # if (args_DEPS)
    #     target_link_libraries(${TARGET} PRIVATE ${args_DEPS})
    # endif ()

    find_program(EMCC emcc REQUIRED HINTS "$ENV{EMSDK}/upstream/emscripten")

    # TODO: this is currently hardcoded to settings that are sensible for most of Halide's
    # internal purposes. Consider adding ways to customize this as appropriate.
    set(EMCC_FLAGS
        -O3
        -std=c++17
        -Wall
        -Wcast-qual
        -Werror
        -Wignored-qualifiers
        -Wno-comment
        -Wno-psabi
        -Wno-unknown-warning-option
        -Wno-unused-function
        -Wsign-compare
        -Wsuggest-override
        -s ASSERTIONS=1
        -s ALLOW_MEMORY_GROWTH=1
        -s ENVIRONMENT=node
        -s STACK_SIZE=98304
        ${args_OPTIONS}
    )

    if ("${Halide_TARGET}" MATCHES "webgpu")
        set(EMCC_FLAGS
            ${EMCC_FLAGS}
            -s USE_WEBGPU=1
            -s ASYNCIFY
        )
    endif ()

    set(SRCS)
    foreach (S IN LISTS args_SRCS)
        list(APPEND SRCS "${CMAKE_CURRENT_SOURCE_DIR}/${S}")
    endforeach ()

    set(INCLUDES)
    foreach (I IN LISTS args_INCLUDES)
        list(APPEND INCLUDES "-I${I}")
    endforeach ()

    set(DEPS)
    foreach (D IN LISTS args_DEPS)
        list(APPEND DEPS $<TARGET_FILE:${D}>)
    endforeach ()

    add_custom_command(OUTPUT "${TARGET}.wasm" "${TARGET}.js"
                       COMMAND ${EMCC} ${EMCC_FLAGS} ${INCLUDES} ${SRCS} ${DEPS} -o "${TARGET}.js"
                       DEPENDS ${SRCS} ${DEPS}
                       VERBATIM)

    add_custom_target("${TARGET}" ALL
                      DEPENDS "${TARGET}.wasm" "${TARGET}.js")

endfunction()

function(add_wasm_halide_test TARGET)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs GROUPS ENABLE_IF)
    cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (args_ENABLE_IF AND NOT (${args_ENABLE_IF}))
        return()
    endif ()

    add_halide_test("${TARGET}"
                    GROUPS ${args_GROUPS}
                    COMMAND ${NODE_JS_EXECUTABLE} "${Halide_SOURCE_DIR}/tools/launch_wasm_test.js" "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.js" "${Halide_TARGET}")
endfunction()

function(find_node_js)
    find_program(NODE_JS_EXECUTABLE node nodejs REQUIRED)

    execute_process(COMMAND "${NODE_JS_EXECUTABLE}" --version
                    OUTPUT_VARIABLE NODE_JS_VERSION_RAW
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REPLACE "v" "" NODE_JS_VERSION ${NODE_JS_VERSION_RAW})

    if (NODE_JS_VERSION VERSION_LESS "16.13")
        message(FATAL_ERROR "Halide requires Node v16.13 or later, but found ${NODE_JS_VERSION_RAW} at ${NODE_JS_EXECUTABLE}. Please set NODE_JS_EXECUTABLE on the CMake command line.")
    endif ()
endfunction()

if (Halide_TARGET MATCHES "wasm")
    # Check and warn up front if a suitable Node.js isn't found
    find_node_js()
endif ()
