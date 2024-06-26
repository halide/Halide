cmake_minimum_required(VERSION 3.22)

# The Hexagon toolchain is broken
set(ENV{HEXAGON_SDK_ROOT} "${HEXAGON_SDK_ROOT}")
set(ENV{HEXAGON_TOOLS_ROOT} "${HEXAGON_TOOLS_ROOT}")

project(halide-hexagon_remote-qurt)

add_library(sim_qurt STATIC sim_qurt.cpp sim_qurt_vtcm.cpp)
target_include_directories(sim_qurt SYSTEM PRIVATE ${HALIDE_HEXAGON_REMOTE_IDL})

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
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_include_directories(hexagon_sim_remote SYSTEM PRIVATE ${HALIDE_HEXAGON_REMOTE_IDL})
target_link_libraries(hexagon_sim_remote PRIVATE ${CMAKE_DL_LIBS})

add_library(
  halide_hexagon_remote_skel
  MODULE
  c11_stubs.cpp
  halide_remote.cpp
  known_symbols.cpp
  log.cpp
  nearbyint.cpp
  ${HALIDE_HEXAGON_REMOTE_IDL}/halide_hexagon_remote_skel.c
)
target_include_directories(halide_hexagon_remote_skel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_include_directories(halide_hexagon_remote_skel SYSTEM PRIVATE ${HALIDE_HEXAGON_REMOTE_IDL})

install(
  TARGETS sim_qurt hexagon_sim_remote halide_hexagon_remote_skel
  DESTINATION bin
)
