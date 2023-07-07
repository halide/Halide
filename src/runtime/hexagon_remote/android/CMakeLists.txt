cmake_minimum_required(VERSION 3.22)
project(halide-hexagon_remote-android)

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
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_include_directories(
  halide_hexagon_host
  SYSTEM PRIVATE
  ${HALIDE_HEXAGON_REMOTE_IDL}
  ${HEXAGON_SDK_ROOT}/incs
  ${HEXAGON_SDK_ROOT}/incs/stddef
)
target_link_libraries(halide_hexagon_host PRIVATE fastrpc::cdsprpc log)

install(
  TARGETS halide_hexagon_host
  DESTINATION bin
)
