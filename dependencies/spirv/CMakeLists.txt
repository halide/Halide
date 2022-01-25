include(FetchContent)
include(CMakeDependentOption)

if (TARGET_VULKAN)
    set(SPIRV_VERSION 1.5.4)

    message(STATUS "Fetching SPIR-V ${SPIRV_VERSION}...")
    FetchContent_Declare(SPIRV-Headers
                         GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
                         GIT_TAG ${SPIRV_VERSION}
                         GIT_SHALLOW TRUE
                         GIT_SUBMODULES "")

    FetchContent_MakeAvailable(SPIRV-Headers)

    set_property(GLOBAL PROPERTY SPIRV_SOURCE_DIR ${spirv-headers_SOURCE_DIR})
    set_property(GLOBAL PROPERTY SPIRV_INCLUDE_DIR ${spirv-headers_SOURCE_DIR}/include)
    
    add_library(Halide_SPIRV INTERFACE)
    target_include_directories(Halide_SPIRV
                               SYSTEM # Use -isystem instead of -I; this is a trick so that clang-tidy won't analyze these includes
                               INTERFACE
                               $<BUILD_INTERFACE:${spirv-headers_SOURCE_DIR}>
                               $<BUILD_INTERFACE:${spirv-headers_SOURCE_DIR}/include>
                               $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/_deps>)
    set_target_properties(Halide_SPIRV PROPERTIES EXPORT_NAME spirv)    
endif ()
