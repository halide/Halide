add_executable(HalideTraceDump HalideTraceDump.cpp HalideTraceUtils.cpp)
target_link_libraries(HalideTraceDump PRIVATE Halide::Halide Halide::ImageIO Halide::Tools)
