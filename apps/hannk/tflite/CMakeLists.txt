add_library(tflite_parser STATIC tflite_parser.cpp)
target_sources(
    tflite_parser
    PUBLIC
    FILE_SET HEADERS
    FILES tflite_parser.h
)
target_link_libraries(
    tflite_parser
    PUBLIC hannk_headers tensorflow-lite::tensorflow-lite interpreter
    PRIVATE Halide::Runtime
)
