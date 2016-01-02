halide_project(interpolate "apps" interpolate.cpp)

target_include_directories(interpolate PRIVATE ${PNG_INCLUDE_DIRS})
target_compile_definitions(interpolate PRIVATE ${PNG_DEFINITIONS})
target_link_libraries(interpolate PRIVATE ${PNG_LIBRARIES})
set_target_properties(interpolate PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                      "${CMAKE_CURRENT_BINARY_DIR}")
