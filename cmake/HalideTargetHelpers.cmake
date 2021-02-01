##
# Utilities for manipulating Halide target triples
##

function(_Halide_get_triple OUTVAR)
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

    set(${OUTVAR} "${arch}-${bits}-${os}" PARENT_SCOPE)
endfunction()

##
# Set Halide `host` and `cmake` meta-target values
##

# This variable is set by package scripts and might differ from Halide_CMAKE_TARGET below.
if (NOT Halide_HOST_TARGET)
    _Halide_cmake_target(Halide_HOST_TARGET)
endif ()

if (NOT Halide_CMAKE_TARGET)
    _Halide_cmake_target(Halide_CMAKE_TARGET)
endif ()

##
# Cache variable to control the global target for add_halide_library.
##

if (NOT "$ENV{HL_TARGET}" STREQUAL "")
    set(Halide_TARGET "$ENV{HL_TARGET}" CACHE STRING "The target to use when compiling AOT tests")
elseif (Halide_HOST_TARGET STREQUAL Halide_CMAKE_TARGET)
    set(Halide_TARGET "host" CACHE STRING "The target to use when compiling AOT tests")
else ()
    set(Halide_TARGET "${Halide_CMAKE_TARGET}" CACHE STRING "The target to use when compiling AOT tests")
endif ()

if (NOT Halide_TARGET_MESSAGE_PRINTED)
    message(STATUS "Halide detected current CMake target:  ${Halide_CMAKE_TARGET}")
    message(STATUS "Halide using default generator target: ${Halide_TARGET}")
    set(Halide_TARGET_MESSAGE_PRINTED TRUE CACHE INTERNAL "Limit printing the detected targets multiple times")
endif ()
