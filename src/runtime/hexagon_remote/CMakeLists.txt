cmake_minimum_required(VERSION 3.22)
project(hexagon_remote)

# We're part of the main Halide build

if (NOT PROJECT_IS_TOP_LEVEL)
  find_package(HexagonSDK REQUIRED)
  include(ExternalProject)

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

  ExternalProject_Add(
    hexagon_remote
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    CMAKE_CACHE_ARGS
      "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${HEXAGON_TOOLCHAIN}"
      "-DHALIDE_HEXAGON_REMOTE_IDL:PATH=${CMAKE_CURRENT_BINARY_DIR}"
    INSTALL_COMMAND ""
    DEPENDS halide_hexagon_remote_idl
    CONFIGURE_HANDLED_BY_BUILD ON
  )

  foreach (abi IN ITEMS armeabi-v7a arm64-v8a)
    ExternalProject_Add(
      halide_hexagon_host-${abi}
      SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
      CMAKE_CACHE_ARGS
        "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${ANDROID_NDK_TOOLCHAIN}"
        "-DHALIDE_HEXAGON_REMOTE_IDL:PATH=${CMAKE_CURRENT_BINARY_DIR}"
        "-DANDROID_ABI:STRING=${abi}"
        "-DANDROID_PLATFORM:STRING=21"
        "-DHEXAGON_SDK_ROOT:PATH=${HEXAGON_SDK_ROOT}"
      INSTALL_COMMAND ""
      DEPENDS halide_hexagon_remote_idl
      CONFIGURE_HANDLED_BY_BUILD ON
    )
  endforeach ()

  # We do not need to use add_custom_command and add_custom_target because this libhalide_hexagon_host.so is built with the native compiler.
  add_library(halide_hexagon_host SHARED sim_host.cpp sim_protocol.h)
  target_compile_features(halide_hexagon_host PRIVATE cxx_std_17)
  target_include_directories(halide_hexagon_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_link_libraries(halide_hexagon_host PRIVATE HexagonSDK::wrapper)

  add_custom_target(build_hexagon_remote)
  add_dependencies(
    build_hexagon_remote
    hexagon_remote
    halide_hexagon_host
    halide_hexagon_host-armeabi-v7a
    halide_hexagon_host-arm64-v8a
  )

  return()
endif ()

# We're in the build phase that uses a known toolchain exclusively.
# These sources should _probably_ be partitioned between subdirectories
# to avoid brittle branching like this.

if (NOT DEFINED HALIDE_HEXAGON_REMOTE_IDL)
  message(FATAL_ERROR "Missing compiled IDL artifacts")
endif ()

if (HEXAGON)
  add_library(sim_qurt STATIC sim_qurt.cpp sim_qurt_vtcm.cpp)
  target_include_directories(sim_qurt PRIVATE ${HALIDE_HEXAGON_REMOTE_IDL})

  add_executable(
    hexagon_sim_remote
    known_symbols.cpp
    sim_remote.cpp
    $<TARGET_OBJECTS:sim_qurt>
  )
  target_include_directories(
    hexagon_sim_remote
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${HALIDE_HEXAGON_REMOTE_IDL}
  )
  target_link_libraries(hexagon_sim_remote PRIVATE ${CMAKE_DL_LIBS})

  add_library(
    halide_hexagon_remote_skel
    MODULE
    halide_remote.cpp
    ${HALIDE_HEXAGON_REMOTE_IDL}/halide_hexagon_remote_skel.c
    nearbyint.cpp
    c11_stubs.cpp
    log.cpp
    known_symbols.cpp
  )
  target_include_directories(
    halide_hexagon_remote_skel
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${HALIDE_HEXAGON_REMOTE_IDL}
  )
elseif (ANDROID)
  set(_aarch64 "")
  if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(_aarch64 "_aarch64")
  endif ()

  add_library(fastrpc::cdsprpc SHARED IMPORTED)
  set_target_properties(
    fastrpc::cdsprpc
    PROPERTIES
    IMPORTED_LOCATION "${HEXAGON_SDK_ROOT}/ipc/fastrpc/remote/ship/android${_aarch64}/libcdsprpc.so"
  )

  add_library(
    halide_hexagon_host
    MODULE
    ${HALIDE_HEXAGON_REMOTE_IDL}/halide_hexagon_remote_stub.c
    host_malloc.cpp
    host_shim.cpp
    libadsprpc_shim.cpp
  )
  target_include_directories(
    halide_hexagon_host
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${HALIDE_HEXAGON_REMOTE_IDL}
    ${HEXAGON_SDK_ROOT}/incs
    ${HEXAGON_SDK_ROOT}/incs/stddef
  )
  target_link_libraries(halide_hexagon_host PRIVATE fastrpc::cdsprpc log)
else ()
  message(FATAL_ERROR "Unknown toolchain!")
endif ()
