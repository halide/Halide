halide_project(unsharp "apps" unsharp.cpp)

target_include_directories(unsharp PRIVATE ${PNG_INCLUDE_DIRS})
target_compile_definitions(unsharp PRIVATE ${PNG_DEFINITIONS})
target_link_libraries(unsharp PRIVATE ${PNG_LIBRARIES})
set_target_properties(unsharp PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                      "${CMAKE_CURRENT_BINARY_DIR}")
