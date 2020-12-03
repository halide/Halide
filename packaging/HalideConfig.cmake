cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

set(${CMAKE_FIND_PACKAGE_NAME}_known_components Halide PNG JPEG)

if (${CMAKE_FIND_PACKAGE_NAME}_FIND_COMPONENTS)
    set(${CMAKE_FIND_PACKAGE_NAME}_comps ${${CMAKE_FIND_PACKAGE_NAME}_FIND_COMPONENTS})
else ()
    # Try to include all components optionally by default
    set(${CMAKE_FIND_PACKAGE_NAME}_comps ${${CMAKE_FIND_PACKAGE_NAME}_known_components})
endif ()

# Allow people to specify explicitly that they only want Halide
list(REMOVE_ITEM ${CMAKE_FIND_PACKAGE_NAME}_comps Halide)

# Parse components for static/shared preference.
foreach (comp IN ITEMS static shared)
    if (comp IN_LIST ${CMAKE_FIND_PACKAGE_NAME}_comps)
        set(${CMAKE_FIND_PACKAGE_NAME}_${comp} YES)
        list(REMOVE_ITEM ${CMAKE_FIND_PACKAGE_NAME}_comps ${comp})
    endif ()
endforeach ()

# Note when both static AND shared are requested
if (${CMAKE_FIND_PACKAGE_NAME}_static AND ${CMAKE_FIND_PACKAGE_NAME}_shared)
    set(${CMAKE_FIND_PACKAGE_NAME}_both TRUE)
endif ()

# Set configured variables
set(Halide_VERSION @Halide_VERSION@)
set(Halide_VERSION_MAJOR @Halide_VERSION_MAJOR@)
set(Halide_VERSION_MINOR @Halide_VERSION_MINOR@)
set(Halide_VERSION_PATCH @Halide_VERSION_PATCH@)
set(Halide_VERSION_TWEAK @Halide_VERSION_TWEAK@)

set(Halide_HOST_TARGET @Halide_HOST_TARGET@)

set(Halide_ENABLE_EXCEPTIONS @Halide_ENABLE_EXCEPTIONS@)
set(Halide_ENABLE_RTTI @Halide_ENABLE_RTTI@)

# Load dependencies from installed configurations
include(CMakeFindDependencyMacro)
find_dependency(Threads)

get_filename_component(_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)
file(GLOB CONFIG_FILES "${_DIR}/Halide-Deps-*.cmake")
foreach (f IN LISTS CONFIG_FILES)
    include(${f})
endforeach ()

# Load common targets that do not depend on shared/static distinction
include("${CMAKE_CURRENT_LIST_DIR}/Halide-Interfaces.cmake")

# Helper to load targets if they exist, report failure, and create aliases when a single type was requested
macro(_Halide_include TYPE CAUSE)
    if (NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/Halide-Targets-${TYPE}.cmake")
        set(${CMAKE_FIND_PACKAGE_NAME}_NOT_FOUND_MESSAGE
            "Could not find Halide ${TYPE} libraries. ${CAUSE}")
        set(${CMAKE_FIND_PACKAGE_NAME}_FOUND FALSE)
        return()
    endif ()

    # Load the namespaced targets
    include("${CMAKE_CURRENT_LIST_DIR}/Halide-Targets-ns-${TYPE}.cmake")

    if (NOT ${CMAKE_FIND_PACKAGE_NAME}_both)
        if (CMAKE_VERSION VERSION_LESS 3.18)
            # In CMake < 3.18, ALIAS targets may not refer to non-global targets, so we
            # are forced to load copies of the targets in the plain Halide:: namespace
            include("${CMAKE_CURRENT_LIST_DIR}/Halide-Targets-${TYPE}.cmake")
        else ()
            foreach (target IN ITEMS Halide Generator RunGenMain Adams2019 Li2018 Mullapudi2016)
                if (TARGET Halide::${TYPE}::${target})
                    add_library(Halide::${target} ALIAS Halide::${TYPE}::${target})
                endif ()
            endforeach ()
        endif ()
    endif ()
endmacro()

# Decide which types to load based on
if (${CMAKE_FIND_PACKAGE_NAME}_static OR ${CMAKE_FIND_PACKAGE_NAME}_shared)
    if (${CMAKE_FIND_PACKAGE_NAME}_shared)
        _Halide_include("shared" "Required by 'shared' component.")
    endif ()
    if (${CMAKE_FIND_PACKAGE_NAME}_static)
        _Halide_include("static" "Required by 'static' component.")
    endif ()
elseif (DEFINED Halide_SHARED_LIBS)
    # Require whatever was requested
    if (Halide_SHARED_LIBS)
        _Halide_include("shared" "Required by Halide_SHARED_LIBS=${Halide_SHARED_LIBS}.")
    else ()
        _Halide_include("static" "Required by Halide_SHARED_LIBS=${Halide_SHARED_LIBS}.")
    endif ()
elseif (BUILD_SHARED_LIBS OR NOT DEFINED BUILD_SHARED_LIBS)
    # Try shared first, then fall back to static.
    # Halide prefers shared by default when BUILD_SHARED_LIBS is not defined,
    # so this is mimicked here.
    if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/Halide-Targets-shared.cmake")
        _Halide_include("shared" "Searched for shared, static.")
    else ()
        _Halide_include("static" "Searched for shared, static.")
    endif ()
else ()
    # Try static first, then fall back to shared
    if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/Halide-Targets-static.cmake")
        _Halide_include("static" "Searched for static, shared.")
    else ()
        _Halide_include("shared" "Searched for static, shared.")
    endif ()
endif ()

# Aliases are not created, so the helpers aren't available.
if (NOT ${CMAKE_FIND_PACKAGE_NAME}_both)
    include("${CMAKE_CURRENT_LIST_DIR}/HalideGeneratorHelpers.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/HalideTargetHelpers.cmake")
endif ()

# Load image library dependencies
foreach (comp IN LISTS ${CMAKE_FIND_PACKAGE_NAME}_comps)
    if (NOT ${comp} IN_LIST ${CMAKE_FIND_PACKAGE_NAME}_known_components)
        set(${CMAKE_FIND_PACKAGE_NAME}_NOT_FOUND_MESSAGE
            "Halide does not recognize requested component: ${comp}")
        set(${CMAKE_FIND_PACKAGE_NAME}_FOUND FALSE)
        return()
    endif ()

    # ${comp} is either PNG or JPEG, and this works for both packages
    if (NOT TARGET ${comp}::${comp})
        set(extraArgs "")
        if (${CMAKE_FIND_PACKAGE_NAME}_FIND_QUIETLY)
            list(APPEND extraArgs QUIET)
        endif ()
        if (${CMAKE_FIND_PACKAGE_NAME}_FIND_REQUIRED_${comp})
            list(APPEND extraArgs REQUIRED)
        endif ()
        find_package(${comp} ${extraArgs})
    endif ()
endforeach ()
