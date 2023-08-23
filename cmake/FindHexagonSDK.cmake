include(FindPackageHandleStandardArgs)

##
# Find the Hexagon SDK root

# We use the presense of the hexagon toolchain file to determine the SDK
# root. Other files have names that are too generic (like readme.txt) or
# are platform-specific (like setup_sdk_env.source) to and so can't be
# used to autodetect the path. Plus, we need to find this file anyway.

find_path(
    HEXAGON_SDK_ROOT build/cmake/hexagon_toolchain.cmake
    HINTS ENV HEXAGON_SDK_ROOT
)

##
# Detect the installed Hexagon tools version

if (NOT DEFINED HEXAGON_TOOLS_VER AND DEFINED ENV{HEXAGON_TOOLS_VER})
    set(HEXAGON_TOOLS_VER "$ENV{HEXAGON_TOOLS_VER}")
endif ()

if (NOT DEFINED HEXAGON_TOOLS_VER)
    # No other way to list a directory; no need for CONFIGURE_DEPENDS here
    # since this is just used to initialize a cache variable.
    file(
        GLOB tools_versions
        RELATIVE "${HEXAGON_SDK_ROOT}/tools/HEXAGON_Tools"
        "${HEXAGON_SDK_ROOT}/tools/HEXAGON_Tools/*"
    )
    if (NOT tools_versions STREQUAL "")
        list(GET tools_versions 0 HEXAGON_TOOLS_VER)
    endif ()
endif ()

set(HEXAGON_TOOLS_VER "${HEXAGON_TOOLS_VER}"
    CACHE STRING "Version of the Hexagon tools to use")

set(HEXAGON_TOOLS_ROOT "${HEXAGON_SDK_ROOT}/tools/HEXAGON_Tools/${HEXAGON_TOOLS_VER}")

##
# Set known paths

set(HEXAGON_TOOLCHAIN ${HEXAGON_SDK_ROOT}/build/cmake/hexagon_toolchain.cmake)
set(HEXAGON_QAIC ${HEXAGON_SDK_ROOT}/ipc/fastrpc/qaic/Ubuntu16/qaic)

set(ANDROID_NDK_ROOT ${HEXAGON_SDK_ROOT}/tools/android-ndk-r19c)
set(ANDROID_NDK_TOOLCHAIN ${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake)

##
# Find ISS wrapper library and headers

find_library(
  HEXAGON_ISS_WRAPPER_LIBRARY
  NAMES wrapper
  HINTS "${HEXAGON_TOOLS_ROOT}"
  PATH_SUFFIXES Tools/lib/iss lib/iss iss
)

find_path(
  HEXAGON_ISS_WRAPPER_INCLUDE_DIRECTORY
  NAMES HexagonWrapper.h
  HINTS "${HEXAGON_TOOLS_ROOT}"
  PATH_SUFFIXES Tools/include/iss include/iss iss
)

##
# Validate we found everything correctly

find_package_handle_standard_args(
    HexagonSDK
    REQUIRED_VARS
        HEXAGON_SDK_ROOT
        HEXAGON_TOOLS_ROOT
        HEXAGON_TOOLCHAIN
        HEXAGON_ISS_WRAPPER_LIBRARY
        HEXAGON_ISS_WRAPPER_INCLUDE_DIRECTORY
    HANDLE_COMPONENTS
)

##
# Create imported targets

if (HexagonSDK_FOUND AND NOT TARGET HexagonSDK::wrapper)
    add_library(HexagonSDK::wrapper UNKNOWN IMPORTED)
    set_target_properties(
        HexagonSDK::wrapper
        PROPERTIES
        IMPORTED_LOCATION "${HEXAGON_ISS_WRAPPER_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${HEXAGON_ISS_WRAPPER_INCLUDE_DIRECTORY}"
    )
endif ()
