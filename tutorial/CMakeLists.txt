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

ADD_CUSTOM_COMMAND(
    TARGET lesson_10_aot_compilation_generate
    POST_BUILD
    COMMAND cd ${CMAKE_BINARY_DIR}/include && ${CMAKE_BINARY_DIR}/bin/lesson_10_aot_compilation_generate
)

ADD_DEPENDENCIES(
    lesson_10_aot_compilation_run
    lesson_10_aot_compilation_generate
)

target_link_libraries(
    lesson_10_aot_compilation_run
    ${CMAKE_BINARY_DIR}/include/lesson_10_halide.o
)
