##
# Utilities for manipulating Halide target triples
##

function(_Halide_cmake_target OUTVAR)
    # Get arch from CMake
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" arch)
    list(TRANSFORM arch REPLACE "^.*(x86|arm|mips|powerpc|hexagon|wasm|riscv).*$" "\\1")
    list(TRANSFORM arch REPLACE "^i.?86.*$" "x86")
    list(TRANSFORM arch REPLACE "^(amd|ia|em)64t?$" "x86")
    list(TRANSFORM arch REPLACE "^ppc(64(le)?)?$" "powerpc")
    list(TRANSFORM arch REPLACE "^aarch(64)?$" "arm")

    # Get bits from CMake
    math(EXPR bits "8 * ${CMAKE_SIZEOF_VOID_P}")

    # Get OS from CMake
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" os)
    list(TRANSFORM os REPLACE "^darwin$" "osx")
    list(TRANSFORM os REPLACE "^emscripten$" "wasmrt")

    # Fix up emscripten usage
    if (os STREQUAL "wasmrt" AND arch STREQUAL "x86")
        set(arch "wasm")
    endif ()

    set(${OUTVAR} "${arch}-${bits}-${os}" PARENT_SCOPE)
endfunction()

function(_Halide_cache var val doc)
    if (DEFINED ${var})
        set(${var} "${${var}}" CACHE STRING "${doc}")
    else ()
        set(${var} "${val}" CACHE STRING "${doc}")
    endif ()
endfunction()

##
# Set Halide `host` and `cmake` meta-target values
##

_Halide_cmake_target(_active_triple)

_Halide_cache(Halide_HOST_TARGET "${_active_triple}" "Halide target triple matching the Halide library")
_Halide_cache(Halide_CMAKE_TARGET "${_active_triple}" "Halide target triple matching the CMake target")

unset(_active_triple)

##
# Cache variable to control the global target for add_halide_library.
##

if (NOT "$ENV{HL_TARGET}" STREQUAL "")
    set(_default_target "$ENV{HL_TARGET}")
elseif (Halide_HOST_TARGET STREQUAL Halide_CMAKE_TARGET)
    set(_default_target "host")
else ()
    set(_default_target "${Halide_CMAKE_TARGET}")
endif ()

_Halide_cache(Halide_TARGET "${_default_target}" "The default target to use when AOT compiling")

unset(_default_target)

##
# Print the active values of all special target triples.
##

get_property(${CMAKE_FIND_PACKAGE_NAME}_MESSAGE_PRINTED GLOBAL PROPERTY ${CMAKE_FIND_PACKAGE_NAME}_MESSAGE_PRINTED)
if (NOT ${CMAKE_FIND_PACKAGE_NAME}_FIND_QUIETLY AND NOT ${CMAKE_FIND_PACKAGE_NAME}_MESSAGE_PRINTED)
    message(STATUS "Halide 'host' platform triple:   ${Halide_HOST_TARGET}")
    message(STATUS "Halide 'cmake' platform triple:  ${Halide_CMAKE_TARGET}")
    message(STATUS "Halide default AOT target:       ${Halide_TARGET}")
    set_property(GLOBAL PROPERTY ${CMAKE_FIND_PACKAGE_NAME}_MESSAGE_PRINTED 1)
endif ()
