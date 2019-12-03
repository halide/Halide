#halide_project(HalideTraceViz "utils" HalideTraceViz.cpp)
halide_project(HalideTraceDump "utils" HalideTraceDump.cpp HalideTraceUtils.cpp)
halide_use_image_io(HalideTraceDump)

  add_executable(HalideTraceViz HalideTraceViz.cpp)
  message(WARNING "HALIDE_COMPILER_LIB is ${HALIDE_COMPILER_LIB}")
  message(WARNING "CMAKE_DL_LIBS is ${CMAKE_DL_LIBS}")
  message(WARNING "CMAKE_THREAD_LIBS_INIT is ${CMAKE_THREAD_LIBS_INIT}")
  target_link_libraries(HalideTraceViz PRIVATE ${HALIDE_COMPILER_LIB} ${CMAKE_DL_LIBS}  ${CMAKE_THREAD_LIBS_INIT})
