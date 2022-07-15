include(FetchContent)
include(CMakeDependentOption)

cmake_dependent_option(FETCH_SPIRV_HEADERS "Fetch SPIR-V headers from remote repo (disable and use SPIRV-Headers_SOURCE_DIR to use an existing checkout)." ON TARGET_SPIRV ON)

if (TARGET_SPIRV)
    if (DEFINED SPIRV-Headers_SOURCE_DIR)
        if (IS_DIRECTORY ${SPIRV-Headers_SOURCE_DIR})
            set(HALIDE_SPIRV_SOURCE_DIR ${SPIRV-Headers_SOURCE_DIR})
            set(HALIDE_SPIRV_INCLUDE_DIR ${SPIRV-Headers_SOURCE_DIR}/include)    
        else()
            message(FATAL_ERROR "SPIRV-Headers were not found - please specify a valid path with 'SPIRV-Headers_SOURCE_DIR'!")
        endif()
    elseif(FETCH_SPIRV_HEADERS)
        set(SPIRV_VERSION 1.5.4)
        message(STATUS "Fetching SPIR-V ${SPIRV_VERSION}...")
        FetchContent_Declare(SPIRV-Headers
                            GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
                            GIT_TAG ${SPIRV_VERSION}
                            GIT_SHALLOW TRUE
                            GIT_SUBMODULES "")

        FetchContent_MakeAvailable(SPIRV-Headers)

        set(HALIDE_SPIRV_SOURCE_DIR ${SPIRV-Headers_SOURCE_DIR})
        set(HALIDE_SPIRV_INCLUDE_DIR ${SPIRV-Headers_SOURCE_DIR}/include)    
    endif()

    if (DEFINED HALIDE_SPIRV_INCLUDE_DIR)
        add_library(Halide_SPIRV INTERFACE)
        target_include_directories(Halide_SPIRV SYSTEM INTERFACE ${HALIDE_SPIRV_INCLUDE_DIR})
        message(STATUS "Using SPIR-V headers from ${HALIDE_SPIRV_INCLUDE_DIR}...")
    else()
        message(FATAL_ERROR "SPIRV-Headers were not found - please enable FETCH_SPIRV_HEADERS or specify a valid path with 'SPIRV-Headers_SOURCE_DIR'!")
    endif()

endif()
