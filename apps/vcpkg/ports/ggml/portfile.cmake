vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ggml-org/ggml
    REF d7cb574130e6f01ad25b3289685489200febcd74  # v0.26.0
    SHA512 8b9c1c962942ae8181232b7ba1276c73120d9f9a3767597c6af72bb97ccc1cf23d7bcee4c1359a4b141c00d74cb04afbdb675d6b26572e9b30ec1adb29b8959d
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
        -DGGML_CPU_KLEIDIAI=ON "-DFETCHCONTENT_SOURCE_DIR_KLEIDIAI=${KLEIDIAI_SOURCE_PATH}"
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
