include(ExternalProject)

find_package(HexagonSDK REQUIRED)

add_custom_command(
  OUTPUT
    halide_hexagon_remote.h
    halide_hexagon_remote_skel.c
    halide_hexagon_remote_stub.c
  COMMAND ${HEXAGON_QAIC} -I ${HEXAGON_SDK_ROOT}/incs/stddef ${CMAKE_CURRENT_SOURCE_DIR}/halide_hexagon_remote.idl
  DEPENDS halide_hexagon_remote.idl
  VERBATIM
)

add_custom_target(
  halide_hexagon_remote_idl
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

if (CMAKE_BUILD_TYPE)
  list(APPEND common_cache_args "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}")
endif ()

ExternalProject_Add(
  hexagon_remote-qurt
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/qurt"
  CMAKE_CACHE_ARGS
    "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${HEXAGON_TOOLCHAIN}"
    ${common_cache_args}
  PREFIX hexagon
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
    DEPENDS halide_hexagon_remote_idl
    CONFIGURE_HANDLED_BY_BUILD ON
  )
endforeach ()

add_library(halide_hexagon_host SHARED sim_host.cpp sim_protocol.h)
target_compile_features(halide_hexagon_host PRIVATE cxx_std_17)
target_include_directories(halide_hexagon_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(halide_hexagon_host PRIVATE HexagonSDK::wrapper)

add_custom_target(hexagon_remote)
add_dependencies(
  hexagon_remote
  hexagon_remote-qurt
  halide_hexagon_host
  halide_hexagon_host-armeabi-v7a
  halide_hexagon_host-arm64-v8a
)
