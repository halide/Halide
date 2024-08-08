include(FetchContent)

FetchContent_Declare(
    flatbuffers
    GIT_REPOSITORY https://github.com/google/flatbuffers.git
    GIT_TAG 0100f6a5779831fa7a651e4b67ef389a8752bd9b # v23.5.26
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG 5b0a6fc2017fcc176545afe3e09c9f9885283242 # v2.10.4
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    wabt
    GIT_REPOSITORY https://github.com/WebAssembly/wabt.git
    GIT_TAG 3e826ecde1adfba5f88d10d361131405637e65a3 # 1.0.36
    GIT_SHALLOW TRUE
)

macro(Halide_provide_dependency method dep_name)
    set(${dep_name}_FOUND 1)

    ## Set up sub-builds for Halide's requirements
    if ("${dep_name}" STREQUAL "flatbuffers")
        set(FLATBUFFERS_BUILD_TESTS OFF)
        set(FLATBUFFERS_INSTALL OFF)
    elseif ("${dep_name}" STREQUAL "pybind11")
        # No special build options necessary
    elseif ("${dep_name}" STREQUAL "wabt")
        set(WITH_EXCEPTIONS "${Halide_ENABLE_EXCEPTIONS}")
        set(BUILD_TESTS OFF)
        set(BUILD_TOOLS OFF)
        set(BUILD_LIBWASM OFF)
        set(USE_INTERNAL_SHA256 ON)
    else ()
        set(${dep_name}_FOUND 0)
    endif ()

    if (${dep_name}_FOUND)
        list(APPEND Halide_provide_dependency_args "${method}" "${dep_name}")
        FetchContent_MakeAvailable(${dep_name})
        list(POP_BACK Halide_provide_dependency_args method dep_name)

        ## Patches for broken packages
        if ("${dep_name}" STREQUAL "flatbuffers")
            if (NOT TARGET flatbuffers::flatbuffers)
                add_library(flatbuffers::flatbuffers ALIAS flatbuffers)
                add_executable(flatbuffers::flatc ALIAS flatc)
            endif ()
        endif ()
        if ("${dep_name}" STREQUAL "wabt")
            set_target_properties(wabt PROPERTIES POSITION_INDEPENDENT_CODE ON)
        endif ()
    endif ()
endmacro()

cmake_language(
    SET_DEPENDENCY_PROVIDER Halide_provide_dependency
    SUPPORTED_METHODS FIND_PACKAGE
)
