halide_project(interpolate "apps" 
  interpolate.cpp)

target_link_libraries(
    interpolate
    png
)
