vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO bytecodealliance/wasm-micro-runtime
    REF 8c18e3f68b16c4bcaf05996b2636f6ed2b4cf629  # WAMR-2.4.4
    SHA512 2378ab44e6ea3cd9bfede86a413c5d5503b8cd0d072bbee7099bd149897a58d74b57c06214f6b163242f5ac8bcdbb81a59632016ebd4c12a717786e1c387c9e3
    PATCHES remove-fetchcontent.patch
)
file(READ "${SOURCE_PATH}/CMakeLists.txt" cmake_contents)
string(REPLACE
    "project (iwasm LANGUAGES C)"
    "project (iwasm LANGUAGES C)\nif (MSVC)\n  enable_language(ASM_MASM)\nendif ()"
    cmake_contents
    "${cmake_contents}"
)
file(WRITE "${SOURCE_PATH}/CMakeLists.txt" "${cmake_contents}")

file(READ "${SOURCE_PATH}/core/shared/platform/include/platform_wasi_types.h" wasi_types_contents)
string(REPLACE
    "/* clang-format off */"
    "/* clang-format off */\n#if defined(_MSC_VER) && !defined(__cplusplus)\n#undef _Alignof\n#define _Alignof(Type) offsetof(struct { char c; Type member; }, member)\n#endif"
    wasi_types_contents
    "${wasi_types_contents}"
)
file(WRITE "${SOURCE_PATH}/core/shared/platform/include/platform_wasi_types.h" "${wasi_types_contents}")


file(READ "${SOURCE_PATH}/build-scripts/version.cmake" version_cmake_contents)
string(REGEX REPLACE
    "configure_file\\([^)]*\\)"
    "#configure_file_removed_by_vcpkg"
    version_cmake_contents
    "${version_cmake_contents}"
)
file(WRITE "${SOURCE_PATH}/build-scripts/version.cmake" "${version_cmake_contents}")


if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x86")
    set(WAMR_BUILD_TARGET "X86_32")
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    set(WAMR_BUILD_TARGET "X86_64")
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm")
    set(WAMR_BUILD_TARGET "ARM")
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64" OR VCPKG_TARGET_ARCHITECTURE STREQUAL "aarch64")
    set(WAMR_BUILD_TARGET "AARCH64")
else()
    message(FATAL_ERROR "Unsupported architecture: ${VCPKG_TARGET_ARCHITECTURE}")
endif()

if(VCPKG_TARGET_IS_LINUX)
    set(WAMR_BUILD_PLATFORM "linux")
elseif(VCPKG_TARGET_IS_WINDOWS)
    set(WAMR_BUILD_PLATFORM "windows")
elseif(VCPKG_TARGET_IS_OSX)
    set(WAMR_BUILD_PLATFORM "darwin")
else()
    message(FATAL_ERROR "Unsupported target platform")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DWAMR_BUILD_TARGET=${WAMR_BUILD_TARGET}
        -DWAMR_BUILD_PLATFORM=${WAMR_BUILD_PLATFORM}
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME iwasm CONFIG_PATH lib/cmake/iwasm)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

file(REMOVE_RECURSE
     "${CURRENT_PACKAGES_DIR}/debug/include"
     "${CURRENT_PACKAGES_DIR}/debug/share"
)
