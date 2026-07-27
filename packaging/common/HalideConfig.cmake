cmake_minimum_required(VERSION 3.28)
@PACKAGE_INIT@

macro(Halide_fail)  # nolint -- required for find_package scoping
    string(JOIN " " ${CMAKE_FIND_PACKAGE_NAME}_NOT_FOUND_MESSAGE ${ARGN})
    set(${CMAKE_FIND_PACKAGE_NAME}_FOUND FALSE)
    return()
endmacro()

macro(Halide_find_component_dependency comp dep)  # nolint
    set(Halide_quiet)
    if (${CMAKE_FIND_PACKAGE_NAME}_FIND_QUIETLY)
        set(Halide_quiet QUIET)
    endif ()

    find_package(${dep} ${ARGN} ${Halide_quiet})

    if (NOT ${dep}_FOUND AND ${CMAKE_FIND_PACKAGE_NAME}_FIND_REQUIRED_${comp})
        Halide_fail(
            "${CMAKE_FIND_PACKAGE_NAME} could not be found because dependency ${dep} could not be found."
        )
    endif ()
endmacro()

set(Halide_known_components JIT Python PNG JPEG static shared)
set(Halide_components PNG JPEG)

foreach (Halide_comp IN LISTS Halide_known_components)
    set(Halide_comp_${Halide_comp} NO)
endforeach ()

if (${CMAKE_FIND_PACKAGE_NAME}_FIND_COMPONENTS)
    set(Halide_components ${${CMAKE_FIND_PACKAGE_NAME}_FIND_COMPONENTS})
endif ()

# Parse components. `JIT` and `Python` force the compiled HalideCompiler package
# to be loaded below, even while cross-compiling. `PNG`/`JPEG` are resolved
# locally (Halide::ImageIO is header-only and lives in this package). `static`/
# `shared` merely record a preference for whichever package eventually loads
# the compiled compiler -- see below.
foreach (Halide_comp IN LISTS Halide_components)
    if (Halide_comp IN_LIST Halide_known_components)
        set(Halide_comp_${Halide_comp} YES)
    else ()
        Halide_fail("Halide does not recognize component `${Halide_comp}`.")
    endif ()
endforeach ()

if (Halide_comp_static AND Halide_comp_shared)
    Halide_fail("Halide `static` and `shared` components are mutually exclusive.")
endif ()

##
# Load the platform-independent helpers: CMake authoring functions and
# header-only/interface targets. This never touches a compiled binary.
##

set(Halide_HOST_TARGET @Halide_HOST_TARGET@)

include(${CMAKE_CURRENT_LIST_DIR}/Halide-targets.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/HalideTargetHelpers.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/HalideGeneratorHelpers.cmake)

##
# Image format components -- resolved locally, never forwarded to HalideCompiler.
##

if (Halide_comp_PNG)
    Halide_find_component_dependency(PNG PNG)
endif ()

if (Halide_comp_JPEG)
    Halide_find_component_dependency(JPEG JPEG)
endif ()

##
# Record a static/shared preference for whichever package eventually loads the
# compiled compiler, without forcing that load to happen now. This is a plain
# (not CACHE, not GLOBAL PROPERTY) variable, so it is naturally scoped to this
# directory and any subdirectories added after this point -- independent
# find_package(Halide ...) calls in unrelated directories of the same project
# can each record their own preference without conflicting. It is
# intentionally NOT unset below with the rest of our internal state, since its
# entire purpose is to still be visible when HalideCompiler is eventually
# loaded, possibly much later (e.g. lazily, from add_halide_generator).
##

if (Halide_comp_static)
    set(_Halide_SHARED_LIBS_preference NO)
elseif (Halide_comp_shared)
    set(_Halide_SHARED_LIBS_preference YES)
endif ()

##
# Optionally load the compiled compiler/JIT package. This always happens for a
# native (non-cross-compiling) build, to preserve historical behavior for the
# common case. While cross-compiling, it only happens if explicitly requested
# via the `JIT` or `Python` components, since the point of this package is to
# be usable without ever touching a platform-specific binary.
##

include(CMakeFindDependencyMacro)

if (Halide_comp_JIT OR Halide_comp_Python OR NOT CMAKE_CROSSCOMPILING)
    set(Halide_compiler_components "")
    if (Halide_comp_Python)
        list(APPEND Halide_compiler_components Python)
    endif ()

    # Forward an explicit static/shared request as a real component here,
    # rather than relying solely on the `_Halide_SHARED_LIBS_preference`
    # variable above. find_dependency() skips its underlying find_package()
    # call when called with arguments identical to a previous call in this
    # directory (or a descendant) -- since that preference variable isn't
    # part of those arguments, two calls that differ only by preference
    # would otherwise collide and silently reuse whichever flavor loaded
    # first, defeating HalideCompilerConfig.cmake's own conflict detection.
    # Forwarding the component keeps this common case (an explicit
    # `static`/`shared` request) correctly distinguished.
    if (Halide_comp_static)
        list(APPEND Halide_compiler_components static)
    elseif (Halide_comp_shared)
        list(APPEND Halide_compiler_components shared)
    endif ()

    if (Halide_compiler_components)
        find_dependency(
            HalideCompiler "@Halide_VERSION@" EXACT HINTS "@PACKAGE_Halide_INSTALL_COMPILERDIR@"
            COMPONENTS ${Halide_compiler_components}
        )
    else ()
        find_dependency(
            HalideCompiler "@Halide_VERSION@" EXACT HINTS "@PACKAGE_Halide_INSTALL_COMPILERDIR@"
        )
    endif ()
endif ()

## Hide variables and helper macros that are not part of our API.

foreach (comp IN LISTS Halide_known_components)
    unset(Halide_comp_${comp})
endforeach ()

unset(Halide_components)
unset(Halide_known_components)
unset(Halide_compiler_components)

# Delete internal macros -- CMake saves redefined macros and functions with a
# single underscore prefixed so, for example, Halide_fail is still available as
# _Halide_fail after one redefinition. Doing it twice overwrites both since the
# saving behavior doesn't continue past the first.
foreach (i RANGE 0 1)
    macro(Halide_fail)  # nolint -- poisoning internal APIs
        message(FATAL_ERROR "Cannot call internal API: Halide_fail")
    endmacro()

    macro(Halide_find_component_dependency)  # nolint
        message(FATAL_ERROR "Cannot call internal API: Halide_find_component_dependency")
    endmacro()
endforeach ()
