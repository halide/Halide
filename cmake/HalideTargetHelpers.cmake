cmake_minimum_required(VERSION 3.28)

##
# Utilities for manipulating Halide target triples
##

function(_Halide_target_arch_os OUT_ARCH OUT_OS raw_arch raw_os)
    string(TOLOWER "${raw_arch}" arch)
    list(TRANSFORM arch REPLACE "^.*(x86|arm|powerpc|hexagon|wasm|riscv).*$" "\\1")
    list(TRANSFORM arch REPLACE "^i.?86.*$" "x86")
    list(TRANSFORM arch REPLACE "^(amd|ia|em)64t?$" "x86")
    list(TRANSFORM arch REPLACE "^ppc(64(le)?)?$" "powerpc")
    list(TRANSFORM arch REPLACE "^aarch(64)?$" "arm")

    string(TOLOWER "${raw_os}" os)
    list(TRANSFORM os REPLACE "^darwin$" "macos")
    list(TRANSFORM os REPLACE "^emscripten$" "wasmrt")

    # Fix up emscripten usage
    if (os STREQUAL "wasmrt" AND arch STREQUAL "x86")
        set(arch "wasm")
    endif ()

    set(${OUT_ARCH} "${arch}" PARENT_SCOPE)
    set(${OUT_OS} "${os}" PARENT_SCOPE)
endfunction()

function(_Halide_format_triple OUTVAR raw_processor raw_os bits)
    _Halide_target_arch_os(arch os "${raw_processor}" "${raw_os}")
    set(triple "${arch}-${bits}-${os}")
    if (os MATCHES "^(ios|macos)$" AND bits EQUAL 64)
        string(TOLOWER "${raw_processor}" processor_lower)
        if (processor_lower STREQUAL "arm64e")
            string(APPEND triple "-arm64e")
        endif ()
    endif ()
    set(${OUTVAR} "${triple}" PARENT_SCOPE)
endfunction()

function(_Halide_host_target OUTVAR)
    cmake_host_system_information(RESULT is_64bit QUERY IS_64BIT)  # nolint -- no alternative for host bitness
    if (is_64bit)
        set(bits 64)
    else ()
        set(bits 32)
    endif ()

    set(host_processor "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_CROSSCOMPILING)
        list(LENGTH CMAKE_OSX_ARCHITECTURES num_osx_archs)
        if (num_osx_archs EQUAL 1 AND CMAKE_OSX_ARCHITECTURES MATCHES "^[Aa][Rr][Mm]64[Ee]$")
            set(host_processor "${CMAKE_OSX_ARCHITECTURES}")
        endif ()
    endif ()

    _Halide_format_triple(triple "${host_processor}" "${CMAKE_HOST_SYSTEM_NAME}" "${bits}")
    set(${OUTVAR} "${triple}" PARENT_SCOPE)
endfunction()

function(_Halide_single_cmake_target OUTVAR processor bits)
    _Halide_format_triple(triple "${processor}" "${CMAKE_SYSTEM_NAME}" "${bits}")
    set(${OUTVAR} "${triple}" PARENT_SCOPE)
endfunction()

function(_Halide_cmake_target OUTVAR)
    math(EXPR bits "8 * ${CMAKE_SIZEOF_VOID_P}")
    if (CMAKE_OSX_ARCHITECTURES)
        set(${OUTVAR} "")
        foreach (processor IN LISTS CMAKE_OSX_ARCHITECTURES)
            _Halide_single_cmake_target(triple "${processor}" "${bits}")
            list(APPEND ${OUTVAR} "${triple}")
        endforeach ()
        list(REMOVE_DUPLICATES ${OUTVAR}) # defensive
    else ()
        _Halide_single_cmake_target(${OUTVAR} "${CMAKE_SYSTEM_PROCESSOR}" "${bits}")
    endif ()
    set(${OUTVAR} "${${OUTVAR}}" PARENT_SCOPE)
endfunction()

##
# Set Halide `host` and `cmake` meta-target values
##

if (NOT DEFINED Halide_HOST_TARGET)
    _Halide_host_target(Halide_HOST_TARGET)
endif ()

set(Halide_HOST_TARGET "${Halide_HOST_TARGET}"
    CACHE STRING "Halide target triple matching the Halide library"
)

if (NOT DEFINED Halide_CMAKE_TARGET)
    _Halide_cmake_target(Halide_CMAKE_TARGET)
endif ()

set(Halide_CMAKE_TARGET "${Halide_CMAKE_TARGET}"
    CACHE STRING "Halide target triple matching the CMake target"
)

##
# Cache variable to control the global target for add_halide_library.
##

if (NOT DEFINED Halide_TARGET)
    if (NOT "$ENV{HL_TARGET}" STREQUAL "")
        set(Halide_TARGET "$ENV{HL_TARGET}")
    elseif (Halide_HOST_TARGET STREQUAL Halide_CMAKE_TARGET)
        set(Halide_TARGET "host")
    else ()
        set(Halide_TARGET "cmake")
    endif ()
endif ()

set(Halide_TARGET "${Halide_TARGET}" CACHE STRING "The default target to use when AOT compiling")

##
# Print the active values of all special target triples.
##

get_property(
    ${CMAKE_FIND_PACKAGE_NAME}_MESSAGE_PRINTED
    GLOBAL PROPERTY
    ${CMAKE_FIND_PACKAGE_NAME}_MESSAGE_PRINTED
)
if (NOT ${CMAKE_FIND_PACKAGE_NAME}_FIND_QUIETLY AND NOT ${CMAKE_FIND_PACKAGE_NAME}_MESSAGE_PRINTED)
    message(STATUS "Halide 'host' platform triple:   ${Halide_HOST_TARGET}")
    message(STATUS "Halide 'cmake' platform triple:  ${Halide_CMAKE_TARGET}")
    message(STATUS "Halide default AOT target:       ${Halide_TARGET}")
    set_property(GLOBAL PROPERTY ${CMAKE_FIND_PACKAGE_NAME}_MESSAGE_PRINTED 1)
endif ()
