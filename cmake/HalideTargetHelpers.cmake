##
# Cache variable to control the global target for add_halide_library.
##

if (NOT "$ENV{HL_TARGET}" STREQUAL "")
    set(Halide_TARGET "$ENV{HL_TARGET}" CACHE STRING "The target to use when compiling AOT tests")
else ()
    set(Halide_TARGET "" CACHE STRING "The target to use when compiling AOT tests")
endif ()

##
# Utilities for manipulating Halide target triples
##

function(_Halide_auto_target OUTVAR)
    _Halide_cmake_target(cmake_target)
    if (Halide_HOST_TARGET STREQUAL cmake_target)
        set(${OUTVAR} host PARENT_SCOPE)
    else ()
        set(${OUTVAR} ${cmake} PARENT_SCOPE)
    endif ()
endfunction()

function(_Halide_triple OUTVAR)
    # Well-formed targets must either start with "host" or a target triple.
    if (ARGN MATCHES "host")
        set(${OUTVAR} ${Halide_HOST_TARGET})
    else ()
        string(REGEX REPLACE "^([^-]+-[^-]+-[^-]+).*$" "\\1" ${OUTVAR} "${ARGN}")
    endif ()
    set(${OUTVAR} "${${OUTVAR}}" PARENT_SCOPE)
endfunction()

function(_Halide_cmake_target OUTVAR)
    # Get arch from CMake
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" arch)
    list(TRANSFORM arch REPLACE  "^.*(x86|arm|mips|powerpc|hexagon|wasm|riscv).*$" "\\1")
    list(TRANSFORM arch REPLACE "^i.?86.+$" "x86")
    list(TRANSFORM arch REPLACE "^(amd|ia|em)64t?$" "x86")
    list(TRANSFORM arch REPLACE "^ppc(64)?$" "powerpc")

    # Get bits from CMake
    math(EXPR bits "8 * ${CMAKE_SIZEOF_VOID_P}")

    # Get OS from CMake
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" os)
    list(TRANSFORM os REPLACE "^darwin$" "osx")

    set(${OUTVAR} "${arch}-${bits}-${os}" PARENT_SCOPE)
endfunction()

##
# Set Halide_HOST_TARGET when we're compiling Halide (this variable is set by package scripts)
##

if (NOT DEFINED Halide_HOST_TARGET)
    _Halide_cmake_target(Halide_HOST_TARGET)
endif ()
