# Generator
halide_project(local_laplacian_gen "apps" local_laplacian_gen.cpp)

set(ll_header "${CMAKE_CURRENT_BINARY_DIR}/local_laplacian.h")
set(ll_obj "${CMAKE_CURRENT_BINARY_DIR}/local_laplacian${CMAKE_CXX_OUTPUT_EXTENSION}")

# Final executable
add_executable(ll_process process.cpp ${ll_header})
target_link_libraries(ll_process PRIVATE ${ll_obj} ${PNG_LIBRARIES})
target_include_directories(ll_process PRIVATE ${PNG_INCLUDE_DIRS})
target_compile_definitions(ll_process PRIVATE ${PNG_DEFINITIONS})

target_include_directories(ll_process PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
if (NOT WIN32)
  target_link_libraries(ll_process PRIVATE dl pthread)
endif()
set_target_properties(ll_process local_laplacian_gen
                      PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                      "${CMAKE_CURRENT_BINARY_DIR}")


add_custom_command(OUTPUT "${ll_header}" "${ll_obj}"
                   COMMAND local_laplacian_gen
                   COMMENT "Generating local_laplacian header and obj file"
                  )
