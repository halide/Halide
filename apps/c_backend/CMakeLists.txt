halide_project(c_backend_pipeline "apps" 
  pipeline.cpp
)

halide_project(c_backend_run "apps" 
  run.cpp
)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

ADD_CUSTOM_COMMAND(
    TARGET c_backend_pipeline
    POST_BUILD
    COMMAND cd ${CMAKE_CURRENT_BINARY_DIR} && ${CMAKE_BINARY_DIR}/bin/c_backend_pipeline
)

ADD_DEPENDENCIES(
    c_backend_run
    c_backend_pipeline
)

add_library(pipeline_c STATIC pipeline_c.cc)

target_link_libraries(
    c_backend_run
    ${CMAKE_CURRENT_BINARY_DIR}/pipeline_native.o
    pipeline_c
)
