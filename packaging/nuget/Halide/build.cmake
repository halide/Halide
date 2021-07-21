include(${Halide_USE_PACKAGE}/CPackConfig.cmake)
include("${CMAKE_CURRENT_LIST_DIR}/../common/config.cmake")

configure_file(${CMAKE_CURRENT_LIST_DIR}/Halide.nuspec
               ${CMAKE_CURRENT_BINARY_DIR}/base/Halide.nuspec)
configure_file(${CMAKE_CURRENT_LIST_DIR}/runtime.json
               ${CMAKE_CURRENT_BINARY_DIR}/base/runtime.json)

find_program(NUGET_EXE nuget REQUIRED)
execute_process(COMMAND ${NUGET_EXE} pack ${CMAKE_CURRENT_BINARY_DIR}/base)
