halide_project(HalideTraceViz "utils" HalideTraceViz.cpp)
halide_project(HalideTraceDump "utils" HalideTraceDump.cpp HalideTraceUtils.cpp)
target_link_libraries(HalideTraceDump PRIVATE Halide::ImageIO)