option(WITH_TUTORIALS "Build tutorials" ON)

include_directories ("${CMAKE_BINARY_DIR}/include")
link_directories ("${LLVM_LIB}")

if (WITH_TUTORIALS)
  file(GLOB TUTORIALS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/" ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
  foreach(file ${TUTORIALS})  
    string(REPLACE ".cpp" "" name ${file})
    halide_project(${name} "tutorials" ${file})
  endforeach()
endif()
