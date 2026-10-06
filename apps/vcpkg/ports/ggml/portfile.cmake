vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ggml-org/ggml
    REF eced84c86f8b012c752c016f7fe789adea168e1e  # v0.15.3
    SHA512 3295c064aff295b0387249d5dec7860b620de82c8361197888df186be18270ede253ab7bce3358b1fb3020f11d01f0f8a29f7d268bff44666f8a8f3ea832781e
)

# Benchmark baseline: GGML's best paths on the build machine (native ISA,
# CPU repack, KleidiAI on arm64, Metal on macOS). Backends are linked, not
# dlopen'ed. KleidiAI is fetched here because vcpkg disables FetchContent.
set(options "")
if (VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
    vcpkg_download_distfile(
        KLEIDIAI_ARCHIVE
        URLS "https://github.com/ARM-software/kleidiai/releases/download/v1.24.0/kleidiai-v1.24.0-src.tar.gz"
        FILENAME "kleidiai-v1.24.0-src.tar.gz"
        SHA512 630994315e4d400ae15291d774dc7ef35b1e154ecd5f80d0dbe7b5a1577c03cfe8ac494cb14f5b1f5f43f8d6ccf1483fec7db71082e741dc9730a1f9f1485068
    )
    vcpkg_extract_source_archive(KLEIDIAI_SOURCE_PATH ARCHIVE "${KLEIDIAI_ARCHIVE}")
    list(APPEND options
        -DGGML_CPU_KLEIDIAI=ON "-DFETCHCONTENT_SOURCE_DIR_KLEIDIAI_DOWNLOAD=${KLEIDIAI_SOURCE_PATH}"
    )
endif ()
if (VCPKG_TARGET_IS_OSX)
    list(APPEND options -DGGML_METAL=ON -DGGML_METAL_EMBED_LIBRARY=ON)
endif ()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
    ${options}
    -DBUILD_SHARED_LIBS=OFF
    -DGGML_BACKEND_DL=OFF
    -DGGML_NATIVE=ON
    -DGGML_CPU_REPACK=ON
    -DGGML_OPENMP=OFF
    -DGGML_BUILD_TESTS=OFF
    -DGGML_BUILD_EXAMPLES=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME ggml CONFIG_PATH lib/cmake/ggml)
vcpkg_fixup_pkgconfig()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE" "${SOURCE_PATH}/AUTHORS")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
