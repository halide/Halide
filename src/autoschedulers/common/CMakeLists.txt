add_library(Halide_Plugin INTERFACE)
add_library(Halide::Plugin ALIAS Halide_Plugin)
target_include_directories(Halide_Plugin INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(Halide_Plugin INTERFACE Halide::Halide)
