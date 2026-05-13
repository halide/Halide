set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

# Override for dawn port specifically
if(PORT STREQUAL "dawn")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
