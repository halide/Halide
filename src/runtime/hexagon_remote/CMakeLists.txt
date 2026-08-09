include(ExternalProject)
include(GNUInstallDirs)

find_package(HexagonSDK REQUIRED)

add_custom_command(
    OUTPUT
        halide_hexagon_remote.h
        halide_hexagon_remote_skel.c
        halide_hexagon_remote_stub.c
    COMMAND
        ${HEXAGON_QAIC} -I ${HEXAGON_SDK_ROOT}/incs/stddef
        ${CMAKE_CURRENT_SOURCE_DIR}/halide_hexagon_remote.idl
    DEPENDS halide_hexagon_remote.idl
    VERBATIM
)

add_custom_target(halide_hexagon_remote_idl
    DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/halide_hexagon_remote.h
    ${CMAKE_CURRENT_BINARY_DIR}/halide_hexagon_remote_skel.c
    ${CMAKE_CURRENT_BINARY_DIR}/halide_hexagon_remote_stub.c
)

set(common_cache_args
    "-DHALIDE_HEXAGON_REMOTE_IDL:PATH=${CMAKE_CURRENT_BINARY_DIR}"
    "-DHEXAGON_SDK_ROOT:PATH=${HEXAGON_SDK_ROOT}"
    "-DHEXAGON_TOOLS_ROOT:PATH=${HEXAGON_TOOLS_ROOT}"
    "-DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>"
)

# The Hexagon SDK toolchain only handles Release and Debug build types.
# Map other types (e.g. RelWithDebInfo) to the appropriate one.
if (CMAKE_BUILD_TYPE STREQUAL "" OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_hexagon_build_type "${CMAKE_BUILD_TYPE}")
else ()
    set(_hexagon_build_type "Release")
endif ()

if (_hexagon_build_type)
    list(APPEND common_cache_args "-DCMAKE_BUILD_TYPE:STRING=${_hexagon_build_type}")
endif ()

# The ExternalProject install steps run during *this* project's build, writing
# into INSTALL_DIR (see -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> above); the outer
# `cmake --install` does not descend into sub-projects. Point every sub-build at
# one shared staging tree so their qurt/android artifacts merge into a single
# bin/<arch> layout, then relay that tree into the real install prefix with the
# install(DIRECTORY) rule near the end of this file.
set(HEXAGON_REMOTE_STAGE "${CMAKE_CURRENT_BINARY_DIR}/stage")

ExternalProject_Add(
    hexagon_remote-qurt
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/qurt"
    CMAKE_CACHE_ARGS "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${HEXAGON_TOOLCHAIN}" ${common_cache_args}
    PREFIX hexagon
    INSTALL_DIR "${HEXAGON_REMOTE_STAGE}"
    DEPENDS halide_hexagon_remote_idl
    CONFIGURE_HANDLED_BY_BUILD ON
)

set(arm_abis armeabi-v7a arm64-v8a)
set(arm_bits 32 64)
foreach (abi bits IN ZIP_LISTS arm_abis arm_bits)
    ExternalProject_Add(
        halide_hexagon_host-${abi}
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
        CMAKE_CACHE_ARGS
            "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${ANDROID_NDK_TOOLCHAIN}"
            "-DANDROID_ABI:STRING=${abi}"
            "-DANDROID_PLATFORM:STRING=21"
            ${common_cache_args}
        PREFIX arm-${bits}-android
        INSTALL_DIR "${HEXAGON_REMOTE_STAGE}"
        DEPENDS halide_hexagon_remote_idl
        CONFIGURE_HANDLED_BY_BUILD ON
    )
endforeach ()

add_library(halide_hexagon_host SHARED sim_host.cpp sim_protocol.h)
target_compile_features(halide_hexagon_host PRIVATE cxx_std_17)
target_include_directories(halide_hexagon_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(halide_hexagon_host PRIVATE HexagonSDK::wrapper)

# The simulator-host variant is a normal target of this project, so its
# install rule is honored directly by `cmake --install`. It mirrors the
# committed src/runtime/hexagon_remote/bin/host layout.
install(
    TARGETS halide_hexagon_host
    DESTINATION "${CMAKE_INSTALL_BINDIR}/host"
    COMPONENT Halide_Hexagon
)

# The qaic-generated IDL header/skel/stub complete the committed
# src/runtime/hexagon_remote/bin/src layout.
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/halide_hexagon_remote.h"
        "${CMAKE_CURRENT_BINARY_DIR}/halide_hexagon_remote_skel.c"
        "${CMAKE_CURRENT_BINARY_DIR}/halide_hexagon_remote_stub.c"
    DESTINATION "${CMAKE_INSTALL_BINDIR}/src"
    COMPONENT Halide_Hexagon
)

# Relay the staged qurt/android artifacts (produced by the ExternalProject
# install steps during the build) into the real install tree. `cmake --install`
# does not descend into ExternalProjects, so without this only the host variant
# above would be installed. USE_SOURCE_PERMISSIONS keeps the executable bit on
# hexagon_sim_remote.
install(
    DIRECTORY "${HEXAGON_REMOTE_STAGE}/bin/"
    TYPE BIN
    COMPONENT Halide_Hexagon
    USE_SOURCE_PERMISSIONS
)

add_custom_target(hexagon_remote)
add_dependencies(
    hexagon_remote
    hexagon_remote-qurt
    halide_hexagon_host
    halide_hexagon_host-armeabi-v7a
    halide_hexagon_host-arm64-v8a
)
