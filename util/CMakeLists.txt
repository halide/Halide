halide_project(HalideTraceViz "utils" HalideTraceViz.cpp)

halide_project(HalideTraceDump "utils" HalideTraceDump.cpp HalideTraceUtils.cpp)
halide_use_image_io(HalideTraceDump)
