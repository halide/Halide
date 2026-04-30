vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO bytecodealliance/wasm-micro-runtime
    REF 8c18e3f68b16c4bcaf05996b2636f6ed2b4cf629  # WAMR-2.4.4
    SHA512 2378ab44e6ea3cd9bfede86a413c5d5503b8cd0d072bbee7099bd149897a58d74b57c06214f6b163242f5ac8bcdbb81a59632016ebd4c12a717786e1c387c9e3
    PATCHES remove-fetchcontent.patch
)

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME iwasm CONFIG_PATH lib/cmake/iwasm)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

file(REMOVE_RECURSE
     "${CURRENT_PACKAGES_DIR}/debug/include"
     "${CURRENT_PACKAGES_DIR}/debug/share"
)
