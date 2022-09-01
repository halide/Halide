add_library(Halide_SPIRV INTERFACE)
add_library(Halide::SPIRV ALIAS Halide_SPIRV)
target_include_directories(Halide_SPIRV SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
