halide_project(HalideTraceViz "utils" HalideTraceViz.cpp HalideTraceUtils.cpp)
use_image_io(HalideTraceViz)

halide_project(HalideTraceDump "utils" HalideTraceDump.cpp HalideTraceUtils.cpp)
use_image_io(HalideTraceDump)

