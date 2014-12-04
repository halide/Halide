halide_project(bilateral_grid "apps" 
  bilateral_grid.cpp
)

halide_project(bilateral_grid_run "apps" 
  filter.cpp
)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

ADD_CUSTOM_COMMAND(
    TARGET bilateral_grid
    POST_BUILD
    COMMAND cd ${CMAKE_CURRENT_BINARY_DIR} && ${CMAKE_BINARY_DIR}/bin/bilateral_grid 1
)

ADD_DEPENDENCIES(
    bilateral_grid_run
    bilateral_grid
)

target_link_libraries(
    bilateral_grid_run
    ${CMAKE_CURRENT_BINARY_DIR}/bilateral_grid.o
    png
)
