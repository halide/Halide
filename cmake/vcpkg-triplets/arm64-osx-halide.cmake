set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)

# Dawn must be a shared library so it can be loaded via dlopen at runtime.
if (PORT STREQUAL "dawn")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif ()
