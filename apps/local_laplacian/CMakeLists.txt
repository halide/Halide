halide_project(local_laplacian "apps" 
  local_laplacian.cpp
)

halide_project(local_laplacian_run "apps" 
  process.cpp
)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

ADD_CUSTOM_COMMAND(
    TARGET local_laplacian
    POST_BUILD
    COMMAND cd ${CMAKE_CURRENT_BINARY_DIR} && ${CMAKE_BINARY_DIR}/bin/local_laplacian
)

ADD_DEPENDENCIES(
    local_laplacian_run
    local_laplacian
)

target_link_libraries(
    local_laplacian_run
    png
    ${CMAKE_CURRENT_BINARY_DIR}/local_laplacian.o
)
