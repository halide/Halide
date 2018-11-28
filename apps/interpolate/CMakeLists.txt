halide_project(interpolate "apps" interpolate.cpp)
halide_use_image_io(interpolate)
set_target_properties(interpolate PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                      "${CMAKE_CURRENT_BINARY_DIR}")
