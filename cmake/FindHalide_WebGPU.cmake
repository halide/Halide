cmake_minimum_required(VERSION 3.28)

# tip: uncomment this line to get better debugging information if find_library() fails
# set(CMAKE_FIND_DEBUG_MODE TRUE)

if (EXISTS "$ENV{HL_WEBGPU_NATIVE_LIB}")
    set(Halide_WebGPU_NATIVE_LIB "$ENV{HL_WEBGPU_NATIVE_LIB}" CACHE FILEPATH "")
endif ()

# Try to find Dawn via vcpkg's CMake config first
if (NOT TARGET Halide::WebGPU)
    # Look for DawnConfig.cmake in vcpkg installed directory
    if (DEFINED ENV{VCPKG_ROOT})
        set(_dawn_cmake_path "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/share/dawn")
        if (EXISTS "${_dawn_cmake_path}/DawnConfig.cmake")
            # Use find_package to properly resolve dependencies
            find_package(Dawn CONFIG QUIET)
            if (TARGET dawn::webgpu_dawn AND NOT TARGET Halide::WebGPU)
                add_library(Halide::WebGPU ALIAS dawn::webgpu_dawn)
            elseif (TARGET dawn::dawn_public_config AND NOT TARGET Halide::WebGPU)
                get_target_property(_dawn_loc dawn::dawn_public_config IMPORTED_LOCATION)
                if (_dawn_loc)
                    add_library(Halide::WebGPU UNKNOWN IMPORTED)
                    set_target_properties(Halide::WebGPU
                        PROPERTIES
                        IMPORTED_LOCATION "${_dawn_loc}"
                    )
                    get_target_property(
                        _dawn_link_deps dawn::dawn_public_config INTERFACE_LINK_LIBRARIES
                    )
                    if (_dawn_link_deps)
                        set_target_properties(Halide::WebGPU
                            PROPERTIES
                            INTERFACE_LINK_LIBRARIES "${_dawn_link_deps}"
                        )
                    endif ()
                endif ()
            endif ()
        elseif (EXISTS "${_dawn_cmake_path}/DawnTargets.cmake")
            include("${_dawn_cmake_path}/DawnTargets.cmake")
            if (TARGET dawn::webgpu_dawn AND NOT TARGET Halide::WebGPU)
                add_library(Halide::WebGPU ALIAS dawn::webgpu_dawn)
            endif ()
        endif ()
    endif ()
endif ()

# If Dawn wasn't found via vcpkg config, fall back to manual find
if (NOT TARGET Halide::WebGPU)
    # Prefer shared library for runtime loading (dlopen)
    find_library(
        Halide_WebGPU_NATIVE_LIB
        NAMES webgpu_dawn wgpu
        PATHS "${CMAKE_CURRENT_LIST_DIR}/../vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib"
        NO_DEFAULT_PATH
    )
    if (NOT Halide_WebGPU_NATIVE_LIB)
        find_library(Halide_WebGPU_NATIVE_LIB NAMES webgpu_dawn wgpu)
    endif ()

    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(
        Halide_WebGPU
        REQUIRED_VARS Halide_WebGPU_NATIVE_LIB
        HANDLE_COMPONENTS
    )

    if (Halide_WebGPU_NATIVE_LIB)
        add_library(Halide::WebGPU UNKNOWN IMPORTED)
        set_target_properties(Halide::WebGPU
            PROPERTIES
            IMPORTED_LOCATION "${Halide_WebGPU_NATIVE_LIB}"
        )

        if (APPLE)
            set_target_properties(Halide::WebGPU
                PROPERTIES
                INTERFACE_LINK_LIBRARIES
                    "-framework Cocoa;-framework IOKit;-framework Foundation;-framework IOSurface;-framework QuartzCore;-framework Metal"
            )
        endif ()
    endif ()
endif ()
