include(FetchContent)
include(CMakeDependentOption)

cmake_dependent_option(WITH_WABT "Include WABT Interpreter for WASM testing" ON "TARGET_WEBASSEMBLY" OFF)
cmake_dependent_option(WITH_WASM_SHELL "Download a wasm shell (e.g. d8) for testing AOT wasm code." ON "TARGET_WEBASSEMBLY" OFF)

if ("${CMAKE_HOST_SYSTEM_NAME}" STREQUAL "Windows")
    if (WITH_WABT)
        message(STATUS "WITH_WABT is not yet supported on Windows")
        set(WITH_WABT OFF CACHE BOOL "WITH_WABT is not yet supported on Windows" FORCE)
    endif ()

    if (WITH_WASM_SHELL)
        message(STATUS "WITH_WASM_SHELL is not yet supported on Windows")
        set(WITH_WASM_SHELL OFF CACHE BOOL "WITH_WASM_SHELL is not yet supported on Windows" FORCE)
    endif ()
endif ()

if (WITH_WABT)
    set(WABT_VER 1.0.19)

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

    # TODO: we want to require unique prefixes to include these files, to avoid ambiguity;
    # this means we have to prefix with "wabt-src/...", which is less bad than other alternatives,
    # but perhaps we could do better (esp. if wabt was smarter about what it exposed?)
    add_library(wabt-obj INTERFACE)
    target_sources(wabt-obj INTERFACE $<BUILD_INTERFACE:$<TARGET_OBJECTS:wabt>>)
    target_include_directories(wabt-obj INTERFACE
                               $<BUILD_INTERFACE:${wabt_SOURCE_DIR}>
                               $<BUILD_INTERFACE:${wabt_BINARY_DIR}>
                               $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/_deps>)
endif ()

if (WITH_WASM_SHELL)
    # Even if we have the latest Emscripten SDK installed, we can't rely on it having
    # an up-to-date shell for running wasm; it includes a version of Node.js that usually
    # lags on wasm updates, so we need something more recent to reliably test wasm.
    # We'll go with a predictable version of d8 (the command-line shell for v8); this
    # is directly mimicking the approach useed by jsvu.
    # These are hosted at
    if ("${CMAKE_HOST_SYSTEM_NAME}" STREQUAL "Windows")
        set(WASM_SHELL_PLATFORM "win64")
    elseif ("${CMAKE_HOST_SYSTEM_NAME}" STREQUAL "Darwin")
        set(WASM_SHELL_PLATFORM "mac64")
    else ()
        set(WASM_SHELL_PLATFORM "linux64")
    endif ()

    # We want to deliberately choose a stable version (rather than top-of-tree);
    # this might be a canary version (if needed to get the updates to v8 that we need)
    # but should be carefully tested before landing.
    set(WASM_SHELL_VERSION 8.8.97)
    set(WASM_SHELL_URL "https://storage.googleapis.com/chromium-v8/official/canary/v8-${WASM_SHELL_PLATFORM}-rel-${WASM_SHELL_VERSION}.zip")
    message(STATUS "Fetching WASM_SHELL ${WASM_SHELL_URL}...")
    FetchContent_Declare(wasm_shell URL "${WASM_SHELL_URL}")
    FetchContent_MakeAvailable(wasm_shell)

    find_program(D8_PATH d8
                 HINTS "${CMAKE_BINARY_DIR}/_deps/wasm_shell-src")

    if (NOT D8_PATH)
        message(FATAL_ERROR "Could not find or download D8 WASM shell")
    endif ()

    add_executable(d8 IMPORTED GLOBAL)
    set_target_properties(d8 PROPERTIES IMPORTED_LOCATION "${D8_PATH}")
endif ()

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

    set(EMCC_FLAGS
        -O3
        -g
        -std=c++11
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
        -s ENVIRONMENT=shell)

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

    if (NOT WITH_WASM_SHELL)
        message(FATAL_ERROR "WITH_WASM_SHELL must be enabled if testing AOT WASM code.")
    endif ()

    set(WASM_SHELL_FLAGS)
    if (Halide_TARGET MATCHES "wasm_simd128")
        list(APPEND WASM_SHELL_FLAGS "--experimental-wasm-simd")
    endif ()
    if (Halide_TARGET MATCHES "wasm_threads")
        list(APPEND WASM_SHELL_FLAGS "--experimental-wasm-threads")
    endif ()

    add_halide_test("${TARGET}"
                    GROUPS ${args_GROUPS}
                    COMMAND d8 ${WASM_SHELL_FLAGS} "${TARGET}.js")
endfunction()
