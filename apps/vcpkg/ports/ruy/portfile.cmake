vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO google/ruy
    REF 2264753777198e4393fb83c44c693462d57a2be1  # latest (2026-07-22) as of 2026-09-26
    SHA512 f54a01147f2dfe972c17df320175ff8e9dbcebad981ab0efb40c388e8a3f90a97397a01d5bfb5c682f5011943293980ea6351af2a573bceca152511f78dfaecd
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

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
