# TODO: if wabt ever corrects their CMake build, replace the FetchContent business
#  with a proper find module or use their config mode.

function(_FindWABT)
    if (${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION)
        set(WABT_VERSION ${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION})
    else ()
        set(WABT_VERSION 1.0.27)
    endif ()

    include(FetchContent)
    FetchContent_Declare(
        wabt
        GIT_REPOSITORY https://github.com/WebAssembly/wabt.git
        GIT_TAG ${WABT_VERSION}
        GIT_SHALLOW TRUE
        GIT_SUBMODULES ""
    )

    set(WITH_EXCEPTIONS "${WABT_WITH_EXCEPTIONS}")
    set(BUILD_TESTS OFF)
    set(BUILD_TOOLS OFF)
    set(BUILD_LIBWASM OFF)
    FetchContent_MakeAvailable(wabt)

    set_target_properties(wabt PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_options(wabt PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-alloca-larger-than>)

    add_library(Halide::WABT INTERFACE IMPORTED)
    target_sources(Halide::WABT INTERFACE $<TARGET_OBJECTS:wabt>)
    target_include_directories(Halide::WABT INTERFACE ${wabt_SOURCE_DIR} ${wabt_BINARY_DIR} ${CMAKE_BINARY_DIR}/_deps)

    set(WABT_DIR "${wabt_BINARY_DIR}" PARENT_SCOPE)
    set(WABT_CONFIG "${wabt_SOURCE_DIR}/CMakeLists.txt" PARENT_SCOPE)
    set(WABT_VERSION "${WABT_VERSION}" PARENT_SCOPE)
endfunction()

_FindWABT()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WABT CONFIG_MODE)

# Delete the function to prevent it from being improperly called outside this
# module. After the first redefinition, the original function is still present
# as __FindWABT. The second redefinition overwrites __FindWABT so that calling
# either one is a no-op.
function(_FindWABT)
endfunction()

function(_FindWABT)
endfunction()
