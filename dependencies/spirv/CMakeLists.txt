include(FetchContent)
include(CMakeDependentOption)

cmake_dependent_option(FETCH_SPIRV_HEADERS "Fetch SPIR-V headers from remote repo (disable and use SPIRV-Headers_SOURCE_DIR to use an existing checkout)." ON TARGET_VULKAN ON)

if (TARGET_VULKAN)
    if (DEFINED SPIRV-Headers_SOURCE_DIR)
        if (IS_DIRECTORY ${SPIRV-Headers_SOURCE_DIR})
            set(HALIDE_SPIRV_SOURCE_DIR ${SPIRV-Headers_SOURCE_DIR})
            set(HALIDE_SPIRV_INCLUDE_DIR ${SPIRV-Headers_SOURCE_DIR}/include)    
            message(STATUS "Using SPIR-V headers from ${HALIDE_SPIRV_SOURCE_DIR}...")
        else()
            message(FATAL_ERROR "SPIRV-Headers were not found - please specify a valid path with 'SPIRV-Headers_SOURCE_DIR'!")
        endif()
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

    set(HALIDE_SPIRV_SOURCE_DIR ${spirv-headers_SOURCE_DIR})
    set(HALIDE_SPIRV_INCLUDE_DIR ${spirv-headers_SOURCE_DIR}/include)    
endif()

if (DEFINED HALIDE_SPIRV_SOURCE_DIR)
    set_property(GLOBAL PROPERTY SPIRV_SOURCE_DIR ${HALIDE_SPIRV_SOURCE_DIR})
    set_property(GLOBAL PROPERTY SPIRV_INCLUDE_DIR ${HALIDE_SPIRV_INCLUDE_DIR})
    add_library(Halide_SPIRV INTERFACE)
    target_include_directories(Halide_SPIRV
                                SYSTEM # Use -isystem instead of -I; this is a trick so that clang-tidy won't analyze these includes
                                INTERFACE
                                $<BUILD_INTERFACE:${spirv-headers_SOURCE_DIR}>
                                $<BUILD_INTERFACE:${spirv-headers_SOURCE_DIR}/include>
                                $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/_deps>)
    set_target_properties(Halide_SPIRV PROPERTIES EXPORT_NAME spirv)    
else()
    message(FATAL_ERROR "SPIRV-Headers were not found - please enable FETCH_SPIRV_HEADERS or specify a valid path with 'SPIRV-Headers_SOURCE_DIR'!")
endif()

