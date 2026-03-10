set(VCPKG_TARGET_ARCHITECTURE arm)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

set(VCPKG_C_FLAGS "-mfp16-format=ieee -Wno-psabi")
set(VCPKG_CXX_FLAGS "-mfp16-format=ieee -Wno-psabi")

set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/toolchains/linux.cmake")
