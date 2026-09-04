vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ggml-org/ggml
    REF eced84c86f8b012c752c016f7fe789adea168e1e  # v0.15.3
    SHA512 3295c064aff295b0387249d5dec7860b620de82c8361197888df186be18270ede253ab7bce3358b1fb3020f11d01f0f8a29f7d268bff44666f8a8f3ea832781e
)

# We set GGML_BACKEND_DL=OFF to keep the CPU backend linked, not dlopen'ed,
# because apps/ggml needs ggml-cpu's internal symbols at link time.
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
    -DBUILD_SHARED_LIBS=OFF
    -DGGML_BACKEND_DL=OFF
    -DGGML_BUILD_TESTS=OFF
    -DGGML_BUILD_EXAMPLES=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME ggml CONFIG_PATH lib/cmake/ggml)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE" "${SOURCE_PATH}/AUTHORS")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
