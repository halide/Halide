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
    set(WABT_VER 1.0.27)

    message(STATUS "Fetching WABT ${WABT_VER}...")
    FetchContent_Declare(wabt
                         GIT_REPOSITORY https://github.com/WebAssembly/wabt.git
                         GIT_TAG ${WABT_VER}
                         GIT_SHALLOW TRUE
                         GIT_SUBMODULES "")

    # configuration for wabt
    set(WITH_EXCEPTIONS ${Halide_ENABLE_EXCEPTIONS})
    set(BUILD_TESTS OFF)
    set(BUILD_TOOLS OFF)
    set(BUILD_LIBWASM OFF)
    FetchContent_MakeAvailable(wabt)

    set_target_properties(wabt PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # Disable this very-noisy warning in GCC
    target_compile_options(wabt
                           PRIVATE
                           $<$<CXX_COMPILER_ID:GNU>:-Wno-alloca-larger-than>)

    # TODO: we want to require unique prefixes to include these files, to avoid ambiguity;
    # this means we have to prefix with "wabt-src/...", which is less bad than other alternatives,
    # but perhaps we could do better (esp. if wabt was smarter about what it exposed?)
    add_library(Halide_wabt INTERFACE)
    target_sources(Halide_wabt INTERFACE $<BUILD_INTERFACE:$<TARGET_OBJECTS:wabt>>)
    target_include_directories(Halide_wabt
                               SYSTEM # Use -isystem instead of -I; this is a trick so that clang-tidy won't analyze these includes
                               INTERFACE
                               $<BUILD_INTERFACE:${wabt_SOURCE_DIR}>
                               $<BUILD_INTERFACE:${wabt_BINARY_DIR}>
                               $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/_deps>)
    set_target_properties(Halide_wabt PROPERTIES EXPORT_NAME wabt)
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
        -s ENVIRONMENT=node
    )

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
                    COMMAND ${NODE_JS_EXECUTABLE} "${TARGET}.js")
endfunction()

function(find_node_js)
    find_program(NODE_JS_EXECUTABLE node nodejs)

    # TODO: when we eventually upgrade to CMake >= 3.18, replace with REQUIRED in find_program
    if (NOT NODE_JS_EXECUTABLE)
        message(FATAL_ERROR "Could not find nodejs. Please set NODE_JS_EXECUTABLE on the CMake command line.")
    endif ()

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
