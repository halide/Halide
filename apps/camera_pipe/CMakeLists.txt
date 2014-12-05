halide_project(camera_pipe "apps" 
  camera_pipe.cpp
)

halide_project(camera_pipe_run "apps" 
  process.cpp
)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

ADD_CUSTOM_COMMAND(
    TARGET camera_pipe
    POST_BUILD
    COMMAND cd ${CMAKE_CURRENT_BINARY_DIR} && ${CMAKE_BINARY_DIR}/bin/camera_pipe
)

ADD_DEPENDENCIES(
    camera_pipe_run
    camera_pipe
)
