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

set(HalideCompiler_known_components Python static shared)
set(HalideCompiler_components "")

foreach (HalideCompiler_comp IN LISTS HalideCompiler_known_components)
    set(HalideCompiler_comp_${HalideCompiler_comp} NO)
endforeach ()

if (${CMAKE_FIND_PACKAGE_NAME}_FIND_COMPONENTS)
    set(HalideCompiler_components ${${CMAKE_FIND_PACKAGE_NAME}_FIND_COMPONENTS})
endif ()

# Parse components for static/shared preference. Note that HalideCompiler has
# no notion of image-format (PNG/JPEG) components -- Halide::ImageIO is
# header-only and lives entirely in the Halide package.
foreach (HalideCompiler_comp IN LISTS HalideCompiler_components)
    if (HalideCompiler_comp IN_LIST HalideCompiler_known_components)
        set(HalideCompiler_comp_${HalideCompiler_comp} YES)
    else ()
        Halide_fail("HalideCompiler does not recognize component `${HalideCompiler_comp}`.")
    endif ()
endforeach ()

if (HalideCompiler_comp_static AND HalideCompiler_comp_shared)
    Halide_fail("HalideCompiler `static` and `shared` components are mutually exclusive.")
endif ()

# Inform downstreams of potential compatibility issues. For instance, exceptions
# and RTTI must both be enabled to build Python bindings and ASAN builds should
# not be mixed with non-ASAN builds.
set(WITH_AUTOSCHEDULERS "@WITH_AUTOSCHEDULERS@")
set(Halide_ENABLE_EXCEPTIONS "@Halide_ENABLE_EXCEPTIONS@")
set(Halide_ENABLE_RTTI "@Halide_ENABLE_RTTI@")
set(Halide_ASAN_ENABLED "@Halide_ASAN_ENABLED@")

##
## Select static or shared and load CMake scripts
##

set(HalideCompiler_static_targets "${CMAKE_CURRENT_LIST_DIR}/HalideCompiler-static-targets.cmake")
set(HalideCompiler_shared_targets "${CMAKE_CURRENT_LIST_DIR}/HalideCompiler-shared-targets.cmake")

set(HalideCompiler_static_deps "${CMAKE_CURRENT_LIST_DIR}/HalideCompiler-static-deps.cmake")
set(HalideCompiler_shared_deps "${CMAKE_CURRENT_LIST_DIR}/HalideCompiler-shared-deps.cmake")

macro(Halide_load_targets type)  # nolint
    if (NOT EXISTS "${HalideCompiler_${type}_targets}")
        Halide_fail("HalideCompiler `${type}` libraries were requested but not found.")
    endif ()

    include("${HalideCompiler_${type}_targets}")

    # install(EXPORT)-generated target files define their (non-GLOBAL)
    # imported targets with a multiple-inclusion guard that silently no-ops
    # the include() above if Halide::Halide is already visible in this
    # directory scope -- which happens whenever an ancestor directory (not a
    # sibling: imported targets propagate down to subdirectories, not
    # sideways) already loaded the other flavor. Detect that mismatch here
    # and fail loudly rather than silently handing back the wrong flavor.
    if (TARGET Halide::Halide)
        get_target_property(Halide_actual_type Halide::Halide TYPE)
        if ("${type}" STREQUAL "static")
            set(Halide_expected_type STATIC_LIBRARY)
        else ()
            set(Halide_expected_type SHARED_LIBRARY)
        endif ()

        if (NOT Halide_actual_type STREQUAL Halide_expected_type)
            Halide_fail(
                "HalideCompiler `${type}` libraries were requested, but the"
                "${Halide_actual_type} flavor of Halide::Halide was already loaded by an"
                "ancestor directory in this CMake configure. Only one flavor can be loaded"
                "per directory scope (and its subdirectories) -- make sure every"
                "find_package(Halide/HalideCompiler ...) call along this directory's"
                "ancestor chain agrees on static vs. shared."
            )
        endif ()
        unset(Halide_actual_type)
        unset(Halide_expected_type)
    endif ()

    # The generated deps file may find_dependency() packages (e.g. Halide_LLVM,
    # V8) whose Find modules are bundled alongside this config file rather than
    # provided by CMake itself, so make sure they're on CMAKE_MODULE_PATH while
    # it runs.
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")
    include("${HalideCompiler_${type}_deps}" OPTIONAL)
    list(REMOVE_ITEM CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")
endmacro()

# An explicit `static`/`shared` component on this package always wins.
# Otherwise, honor a preference recorded by the `Halide` package -- if a
# caller requested `find_package(Halide COMPONENTS static/shared)` without
# forcing a compiler load, that preference is stashed in a plain (directory-
# and call-site-scoped, see HalideConfig.cmake) variable so it's still
# respected whenever HalideCompiler does eventually get loaded (e.g. lazily,
# from add_halide_generator, in the same directory). Failing that, fall back
# to the ambient `Halide_SHARED_LIBS` variable, then `BUILD_SHARED_LIBS`.
#
# An explicit component that contradicts an already-recorded preference is a
# real conflict, not just an override -- fail loudly rather than silently
# picking one.
if (DEFINED _Halide_SHARED_LIBS_preference)
    if (HalideCompiler_comp_static AND _Halide_SHARED_LIBS_preference)
        Halide_fail(
            "HalideCompiler `static` component conflicts with a `shared`"
            "preference already recorded in this scope (e.g. via"
            "find_package(Halide COMPONENTS shared))."
        )
    elseif (HalideCompiler_comp_shared AND NOT _Halide_SHARED_LIBS_preference)
        Halide_fail(
            "HalideCompiler `shared` component conflicts with a `static`"
            "preference already recorded in this scope (e.g. via"
            "find_package(Halide COMPONENTS static))."
        )
    endif ()
endif ()

if (HalideCompiler_comp_static)
    Halide_load_targets(static)
elseif (HalideCompiler_comp_shared)
    Halide_load_targets(shared)
elseif (DEFINED _Halide_SHARED_LIBS_preference AND _Halide_SHARED_LIBS_preference)
    Halide_load_targets(shared)
elseif (DEFINED _Halide_SHARED_LIBS_preference AND NOT _Halide_SHARED_LIBS_preference)
    Halide_load_targets(static)
elseif (DEFINED Halide_SHARED_LIBS AND Halide_SHARED_LIBS)
    Halide_load_targets(shared)
elseif (DEFINED Halide_SHARED_LIBS AND NOT Halide_SHARED_LIBS)
    Halide_load_targets(static)
elseif (BUILD_SHARED_LIBS OR NOT DEFINED BUILD_SHARED_LIBS)
    if (EXISTS "${HalideCompiler_shared_targets}")
        Halide_load_targets(shared)
    else ()
        Halide_load_targets(static)
    endif ()
else ()
    if (EXISTS "${HalideCompiler_static_targets}")
        Halide_load_targets(static)
    else ()
        Halide_load_targets(shared)
    endif ()
endif ()

## Load Python component
if (HalideCompiler_comp_Python OR "@WITH_PYTHON_BINDINGS@")
    Halide_find_component_dependency(
        Python Halide_Python
        HINTS "@PACKAGE_Halide_Python_INSTALL_CMAKEDIR@"
    )
endif ()

## Hide variables and helper macros that are not part of our API.

# Delete internal component tracking
foreach (comp IN LISTS HalideCompiler_known_components)
    unset(HalideCompiler_comp_${comp})
endforeach ()

unset(HalideCompiler_components)
unset(HalideCompiler_known_components)

# Delete paths to generated CMake files
unset(HalideCompiler_shared_deps)
unset(HalideCompiler_shared_targets)
unset(HalideCompiler_static_deps)
unset(HalideCompiler_static_targets)

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

    macro(Halide_load_targets)  # nolint
        message(FATAL_ERROR "Cannot call internal API: Halide_load_targets")
    endmacro()
endforeach ()
