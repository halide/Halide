cmake_minimum_required(VERSION 3.16)

function(find_node_js)

    if (NOT NODE_JS)
        # If HALIDE_NODE_JS_PATH is specified, always look there, ignoring normal lookup paths;
        # otherwise, just rely on find_program(). (This is fairly essential, since many EMSDK versions
        # include a too-old version of Node that is likely to be in the current search path
        # when EMCC is in use.)
        if ("$ENV{HALIDE_NODE_JS_PATH}" STREQUAL "")
            find_program(NODE_JS node)
        else ()
            get_filename_component(HALIDE_NODE_JS_DIR $ENV{HALIDE_NODE_JS_PATH} DIRECTORY)
            message(STATUS "HALIDE_NODE_JS_DIR ${HALIDE_NODE_JS_DIR}")
            find_program(NODE_JS node
                         NO_DEFAULT_PATH
                         PATHS "${HALIDE_NODE_JS_DIR}")
            message(STATUS "NODE_JS ${NODE_JS}")
        endif ()

        if (NOT NODE_JS)
            message(FATAL_ERROR "Could not find Node.js shell")
        endif ()

        if (DEFINED CACHE{NODE_JS_VERSION_OK})
            message(STATUS "NODE_JS_VERSION_OK is defined")
            return()
        endif()

        execute_process(COMMAND "${NODE_JS}" --version
                        OUTPUT_VARIABLE NODE_JS_VERSION_RAW
                        OUTPUT_STRIP_TRAILING_WHITESPACE)
        string(REPLACE "v" "" NODE_JS_VERSION ${NODE_JS_VERSION_RAW})
        string(REPLACE "." ";" NODE_JS_VERSION ${NODE_JS_VERSION})
        message(STATUS "Found Node.js runtime at ${NODE_JS}, version ${NODE_JS_VERSION_RAW}")

        list(GET NODE_JS_VERSION 0 NODE_JS_MAJOR)
        list(GET NODE_JS_VERSION 1 NODE_JS_MINOR)
        list(GET NODE_JS_VERSION 2 NODE_JS_PATCH)

        if (NODE_JS_MAJOR LESS 16)
            message(FATAL_ERROR "Halide requires Node v16.13 or later, but found ${NODE_JS_VERSION_RAW} at ${NODE_JS}")
        endif ()

        if ((NODE_JS_MAJOR EQUALS 16) AND (NODE_JS_MINOR LESS 13))
            message(FATAL_ERROR "Halide requires Node v16.13 or later, but found ${NODE_JS_VERSION_RAW} at ${NODE_JS}")
        endif ()

        set(NODE_JS_VERSION_OK "YES" CACHE INTERNAL "Node is OK")
    endif ()

endfunction()

function(add_wasm_executable TARGET)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs SRCS DEPS INCLUDES ENABLE_IF)
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

    find_program(EMCC emcc HINTS "$ENV{EMSDK}/upstream/emscripten")

    if (NOT EMCC)
        message(FATAL_ERROR "Building tests or apps for WASM requires that EMSDK point to a valid Emscripten install.")
    endif ()

    # TODO: this is currently hardcoded to settings that are sensible for most of Halide's
    # internal purposes. Consider adding ways to customize this as appropriate.
    set(EMCC_FLAGS
        -O3
        -g
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
        -s WASM_BIGINT=1
        -s STANDALONE_WASM=1
        -s ENVIRONMENT=node)

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
                    COMMAND ${NODE_JS} "${TARGET}.js")
endfunction()
