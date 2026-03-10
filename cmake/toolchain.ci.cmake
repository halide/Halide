# Derive the target architecture and platform from VCPKG_TARGET_TRIPLET
# and chainload the appropriate vcpkg platform toolchain. This ensures that
# the project and its vcpkg dependencies are built for the same architecture.
if(NOT VCPKG_TARGET_TRIPLET)
    return()
endif()

string(REGEX MATCH "^([^-]+)-(.+)$" _match "${VCPKG_TARGET_TRIPLET}")
set(VCPKG_TARGET_ARCHITECTURE "${CMAKE_MATCH_1}")
set(_VCPKG_PLATFORM "${CMAKE_MATCH_2}")

set(_VCPKG_ROOT "$ENV{VCPKG_INSTALLATION_ROOT}")
if(NOT _VCPKG_ROOT)
    set(_VCPKG_ROOT "$ENV{VCPKG_ROOT}")
endif()

include("${_VCPKG_ROOT}/scripts/toolchains/${_VCPKG_PLATFORM}.cmake")

unset(_VCPKG_ROOT)
unset(_VCPKG_PLATFORM)