# This executable does not have any generators registered so it is only capable
# of generating a standalone object file containing Halide's runtime.
#
# This is used for pure runtime tests which need to link against the Halide
# runtime.
halide_project(runtime.generator "generator" GenGen.cpp)

add_library(HalideToolsRunGen "${CMAKE_SOURCE_DIR}/tools/RunGen.cpp")
target_include_directories(HalideToolsRunGen PRIVATE "${CMAKE_SOURCE_DIR}/src/runtime")
target_compile_definitions(HalideToolsRunGen PUBLIC "-D_CRT_SECURE_NO_WARNINGS" "-D_SCL_SECURE_NO_WARNINGS")
halide_use_image_io(HalideToolsRunGen)
if (MSVC)
else()
  target_compile_options(HalideToolsRunGen PUBLIC "-fno-rtti")
endif()
