include(FetchContent)
include(CMakeDependentOption)

cmake_dependent_option(WITH_WABT "Include WABT Interpreter for WASM testing" ON "TARGET_WEBASSEMBLY" OFF)
cmake_dependent_option(WITH_WASM_SHELL "Download a wasm shell (e.g. d8) for testing AOT wasm code." ON "TARGET_WEBASSEMBLY" OFF)

if ("${CMAKE_HOST_SYSTEM_NAME}" STREQUAL "Windows")
    if (WITH_WABT)
        message(STATUS "WITH_WABT is not yet supported on Windows")
        set(WITH_WABT OFF CACHE BOOL "WITH_WABT is not yet supported on Windows" FORCE)
    endif ()
endif ()

if (WITH_WABT)
    set(WABT_VER 1.0.24)

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

if (Halide_TARGET MATCHES "wasm")
    # Check and warn up front if a suitable Node.js isn't found
    find_node_js()
endif ()
