cmake_minimum_required(VERSION 3.22)

macro(Halide_fail message)
    set(${CMAKE_FIND_PACKAGE_NAME}_NOT_FOUND_MESSAGE "${message}")
    set(${CMAKE_FIND_PACKAGE_NAME}_FOUND FALSE)
    return()
endmacro()

macro(Halide_find_component_dependency comp dep)
    set(Halide_quiet)
    if (${CMAKE_FIND_PACKAGE_NAME}_FIND_QUIETLY)
        set(Halide_quiet QUIET)
    endif ()

    set(Halide_required)
    if (${CMAKE_FIND_PACKAGE_NAME}_FIND_REQUIRED_${comp})
        set(Halide_required REQUIRED)
    endif ()

    find_package(${dep} ${ARGN} ${Halide_quiet} ${Halide_required})

    if (NOT ${dep}_FOUND)
        Halide_fail("${CMAKE_FIND_PACKAGE_NAME} could not be found because dependency ${dep} could not be found.")
    endif ()
endmacro()

set(Halide_known_components Halide PNG JPEG static shared)
set(Halide_components Halide PNG JPEG)

foreach (Halide_comp IN LISTS Halide_known_components)
    set(Halide_comp_${Halide_comp} NO)
endforeach ()

if (${CMAKE_FIND_PACKAGE_NAME}_FIND_COMPONENTS)
    set(Halide_components ${${CMAKE_FIND_PACKAGE_NAME}_FIND_COMPONENTS})
endif ()

# Parse components for static/shared preference.
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

# Set configured variables
set(Halide_VERSION @Halide_VERSION@)
set(Halide_VERSION_MAJOR @Halide_VERSION_MAJOR@)
set(Halide_VERSION_MINOR @Halide_VERSION_MINOR@)
set(Halide_VERSION_PATCH @Halide_VERSION_PATCH@)
set(Halide_VERSION_TWEAK @Halide_VERSION_TWEAK@)

set(Halide_ENABLE_EXCEPTIONS @Halide_ENABLE_EXCEPTIONS@)
set(Halide_ENABLE_RTTI @Halide_ENABLE_RTTI@)

##
## Find dependencies based on components
##

include(CMakeFindDependencyMacro)

find_dependency(HalideHelpers "${Halide_VERSION}" EXACT)

if (Halide_comp_PNG)
    Halide_find_component_dependency(PNG PNG)
endif ()

if (Halide_comp_JPEG)
    Halide_find_component_dependency(JPEG JPEG)
endif ()

##
## Select static or shared and load CMake scripts
##

set(Halide_static_targets "${CMAKE_CURRENT_LIST_DIR}/Halide-static-targets.cmake")
set(Halide_shared_targets "${CMAKE_CURRENT_LIST_DIR}/Halide-shared-targets.cmake")

set(Halide_static_deps "${CMAKE_CURRENT_LIST_DIR}/Halide-static-deps.cmake")
set(Halide_shared_deps "${CMAKE_CURRENT_LIST_DIR}/Halide-shared-deps.cmake")

macro(Halide_load_targets type)
    if (NOT EXISTS "${Halide_${type}_targets}")
        Halide_fail("Halide `${type}` libraries were requested but not found.")
    endif ()

    include("${Halide_${type}_targets}")
    include("${Halide_${type}_deps}" OPTIONAL)
endmacro()

if (Halide_comp_static)
    Halide_load_targets(static)
elseif (Halide_comp_shared)
    Halide_load_targets(shared)
elseif (DEFINED Halide_SHARED_LIBS AND Halide_SHARED_LIBS)
    Halide_load_targets(shared)
elseif (DEFINED Halide_SHARED_LIBS AND NOT Halide_SHARED_LIBS)
    Halide_load_targets(static)
elseif (BUILD_SHARED_LIBS OR NOT DEFINED BUILD_SHARED_LIBS)
    if (EXISTS "${Halide_shared_targets}")
        Halide_load_targets(shared)
    else ()
        Halide_load_targets(static)
    endif ()
else ()
    if (EXISTS "${Halide_static_targets}")
        Halide_load_targets(static)
    else ()
        Halide_load_targets(shared)
    endif ()
endif ()
