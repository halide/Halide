# Generator
halide_project(local_laplacian_gen "apps" local_laplacian_gen.cpp)

set(ll_header "${CMAKE_CURRENT_BINARY_DIR}/local_laplacian.h")
set(ll_lib "${CMAKE_CURRENT_BINARY_DIR}/local_laplacian${CMAKE_STATIC_LIBRARY_SUFFIX}")

# Final executable
add_executable(ll_process process.cpp ${ll_header})
target_link_libraries(ll_process PRIVATE ${ll_lib} ${PNG_LIBRARIES})
target_include_directories(ll_process PRIVATE ${PNG_INCLUDE_DIRS})
target_compile_definitions(ll_process PRIVATE ${PNG_DEFINITIONS})

target_include_directories(ll_process PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
if (NOT WIN32)
  target_link_libraries(ll_process PRIVATE dl pthread)
endif()
if (NOT MSVC)
  target_compile_options(ll_process PRIVATE "-std=c++11")
endif()

add_custom_command(OUTPUT "${ll_header}" "${ll_lib}"
                   COMMAND local_laplacian_gen
                   WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                   COMMENT "Generating local_laplacian header and lib file"
                  )
