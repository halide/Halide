include(FetchContent)
include(CMakeDependentOption)

cmake_dependent_option(
    FETCH_SPIRV_HEADERS "Fetch SPIR-V headers from remote repo (disable and use SPIRV-Headers_SOURCE_DIR to use an existing checkout)." ON
    TARGET_SPIRV OFF
)

if (FETCH_SPIRV_HEADERS)
    set(SPIRV_VERSION 1.5.4)
    message(STATUS "Fetching SPIR-V ${SPIRV_VERSION}...")
    FetchContent_Declare(
        SPIRV-Headers
        GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
        GIT_TAG ${SPIRV_VERSION}
        GIT_SHALLOW TRUE
        GIT_SUBMODULES ""
    )
    FetchContent_GetProperties(SPIRV-Headers)
    if (NOT SPIRV-Headers_POPULATED)
        FetchContent_Populate(SPIRV-Headers)
    endif ()
    if (EXISTS ${spirv-headers_SOURCE_DIR})
        set(SPIRV_INCLUDE_DIR "${spirv-headers_SOURCE_DIR}/include")    
    endif()
else ()
    find_path(SPIRV_INCLUDE_DIR spirv/1.0/spirv.h)
endif ()

if (NOT SPIRV_INCLUDE_DIR)
    message(FATAL_ERROR "SPIRV-Headers were not found - please enable FETCH_SPIRV_HEADERS or specify a valid path with 'SPIRV_INCLUDE_DIR'!")
endif ()

message(STATUS "Using SPIR-V headers from ${SPIRV_INCLUDE_DIR}...")
add_library(Halide::SPIRV INTERFACE IMPORTED)
target_include_directories(Halide::SPIRV 
                           SYSTEM # Use -isystem instead of -I; this is a trick so that clang-tidy won't analyze these includes
                           INTERFACE "${SPIRV_INCLUDE_DIR}")
