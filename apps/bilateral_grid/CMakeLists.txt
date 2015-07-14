# Generator
halide_project(bilateral_grid "apps" bilateral_grid.cpp)

set(bilateral_grid_h "${CMAKE_CURRENT_BINARY_DIR}/bilateral_grid.h")
set(bilateral_grid_obj "${CMAKE_CURRENT_BINARY_DIR}/bilateral_grid.o")

# Final executable
add_executable(filter filter.cpp ${bilateral_grid_h})
target_link_libraries(filter PRIVATE ${bilateral_grid_obj} ${PNG_LIBRARIES})
target_include_directories(filter PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
                           "${PNG_INCLUDE_DIRS}")
target_compile_definitions(filter PRIVATE ${PNG_DEFINITIONS})

if (NOT WIN32)
  target_link_libraries(filter PRIVATE dl pthread)
endif()
set_target_properties(filter bilateral_grid PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                      "${CMAKE_CURRENT_BINARY_DIR}")

# FIXME: Cannot use halide_add_generator_dependency() because
# bilateral_grid doesn't handle the commandline args passed.
add_custom_command(OUTPUT "${bilateral_grid_h}" "${bilateral_grid_obj}"
                   COMMAND bilateral_grid 8
                   COMMENT "Generating bilateral_grid"
                  )
