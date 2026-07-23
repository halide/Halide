set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)

# Dawn must be a shared library so it can be loaded via dlopen at runtime.
if (PORT STREQUAL "dawn")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif ()

# Skip Debug builds in CI: they're unused (we only ever load Release Dawn) and
# ~2x the build time / ~30x the artifact size for no benefit.
if (DEFINED ENV{CI})
    set(VCPKG_BUILD_TYPE release)
endif ()
