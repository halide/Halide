cmake_minimum_required(VERSION 3.16...3.21)
project(hannk_superbuild LANGUAGES NONE)

##
# Derive settings for the host and cross builds from the super-build cache.
# Any cache variable matching HANNK_(HOST|CROSS)_VAR will set VAR
# in the corresponding build. All other non-INTERNAL or STATIC cache
# variables are set in the cross build, too.

set(host_args
    -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
    -DHANNK_AOT_HOST_ONLY:BOOL=ON)

set(cross_args
    -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
    -DHANNK_BUILD_TFLITE:BOOL=OFF)

get_property(cache DIRECTORY PROPERTY CACHE_VARIABLES)
foreach (entry IN LISTS cache)
    get_property(type CACHE ${entry} PROPERTY TYPE)

    # Uninitialized cache entries sometimes behave oddly on the first versus
    # second invocation of CMake on versions prior to 3.21. See the docs for
    # more detail: https://cmake.org/cmake/help/latest/policy/CMP0126.html
    if (type STREQUAL "UNINITIALIZED")
        set(type "STRING")
    endif ()

    # Don't copy over CMake internal variables
    if (NOT type MATCHES "STATIC|INTERNAL")
        string(REPLACE ";" "$<SEMICOLON>" value "${${entry}}")
        if (entry MATCHES "^HANNK_HOST_(.+)$")
            list(APPEND host_args "-D${CMAKE_MATCH_1}:${type}=${value}")
        elseif (entry MATCHES "^HANNK_CROSS_(.+)$")
            list(APPEND cross_args "-D${CMAKE_MATCH_1}:${type}=${value}")
        else ()
            list(APPEND cross_args "-D${entry}:${type}=${value}")
        endif ()
    endif ()
endforeach ()

##
# Common ExternalProject options

set(ep_opts
    SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.."
    BUILD_ALWAYS YES
    USES_TERMINAL_CONFIGURE YES
    USES_TERMINAL_BUILD YES)

if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.20)
    list(APPEND ep_opts CONFIGURE_HANDLED_BY_BUILD TRUE)
endif ()

##
# Define host and target builds

include(ExternalProject)

message(STATUS "Using the following settings for host build: ")
foreach (entry IN LISTS host_args)
    string(REPLACE "$<SEMICOLON>" ";" entry "${entry}")
    message(STATUS "\t${entry}")
endforeach ()

ExternalProject_Add(hannk_host ${ep_opts}
                    INSTALL_COMMAND ""  # Disables install stage
                    CMAKE_ARGS ${host_args})

ExternalProject_Get_Property(hannk_host BINARY_DIR)
list(APPEND cross_args "-Dhannk-halide_generators_ROOT:FILEPATH=${BINARY_DIR}")

message(STATUS "Using the following settings for cross build: ")
foreach (entry IN LISTS cross_args)
    string(REPLACE "$<SEMICOLON>" ";" entry "${entry}")
    message(STATUS "\t${entry}")
endforeach ()

ExternalProject_Add(hannk ${ep_opts}
                    INSTALL_COMMAND ""  # Disables install stage
                    CMAKE_ARGS ${cross_args})

ExternalProject_Add_StepDependencies(hannk configure hannk_host)

##
# Create shims to make the super-build directory behave like the cross-build directory

# Don't let CMake generate a cmake_install.cmake file
set(CMAKE_SKIP_INSTALL_RULES YES)

ExternalProject_Get_Property(hannk BINARY_DIR)
file(GENERATE OUTPUT "cmake_install.cmake"
     CONTENT "include(\"${BINARY_DIR}/cmake_install.cmake\")")


# Don't let CMake generate a CTestTestfile.cmake file (don't uncomment!)
# enable_testing()

file(RELATIVE_PATH ctest_dir "${CMAKE_CURRENT_BINARY_DIR}" "${BINARY_DIR}")
file(GENERATE OUTPUT "CTestTestfile.cmake"
     CONTENT "subdirs(\"${ctest_dir}\")")

