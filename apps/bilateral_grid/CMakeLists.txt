# Generator
halide_project(bilateral_grid "apps" bilateral_grid.cpp)

set(bilateral_grid_h "${CMAKE_CURRENT_BINARY_DIR}/bilateral_grid.h")
set(bilateral_grid_lib "${CMAKE_CURRENT_BINARY_DIR}/bilateral_grid${CMAKE_STATIC_LIBRARY_SUFFIX}")

# Final executable
add_executable(filter filter.cpp ${bilateral_grid_h})
target_link_libraries(filter PRIVATE ${bilateral_grid_lib} ${PNG_LIBRARIES})
target_include_directories(filter PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
                           "${PNG_INCLUDE_DIRS}")
target_compile_definitions(filter PRIVATE ${PNG_DEFINITIONS})

if (NOT WIN32)
  target_link_libraries(filter PRIVATE dl pthread)
endif()

if (NOT MSVC)
  target_compile_options(filter PRIVATE "-std=c++11")
endif()

# FIXME: Cannot use halide_add_generator_dependency() because
# bilateral_grid doesn't handle the commandline args passed.
add_custom_command(OUTPUT "${bilateral_grid_h}" "${bilateral_grid_lib}"
                   COMMAND bilateral_grid 8
                   WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                   COMMENT "Generating bilateral_grid"
                  )
