vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO google/ruy
    REF 2af88863614a8298689cc52b1a47b3fcad7be835  # latest (2026-02-16) as of 2026-02-24
    SHA512 e2cb19d7cc98c29b86a02971cb98941954eee05505ff1600e3eebbd0ec85afad01c22eed7d49d9948fde9f83413704f86b58f4a10c5dedcb0bbfaf854fa25242
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
    -DRUY_MINIMAL_BUILD=ON
    -DRUY_ENABLE_INSTALL=ON
    -DRUY_FIND_CPUINFO=ON
    -DRUY_PROFILER=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME ruy CONFIG_PATH lib/cmake/ruy)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

file(REMOVE_RECURSE
     "${CURRENT_PACKAGES_DIR}/debug/include"
     "${CURRENT_PACKAGES_DIR}/debug/share"
)
