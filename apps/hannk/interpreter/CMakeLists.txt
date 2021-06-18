
add_library(elementwise_program STATIC
            elementwise_program.cpp)
target_include_directories(elementwise_program PUBLIC $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)
target_link_libraries(elementwise_program PRIVATE Halide::Runtime)

add_library(interpreter_lower STATIC
            lower.cpp)
target_include_directories(interpreter_lower PUBLIC $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)
target_link_libraries(interpreter_lower PRIVATE Halide::Runtime)

add_library(interpreter STATIC
            interpreter.cpp
            interval.cpp
            model.cpp
            ops.cpp
            tensor.cpp
            transforms.cpp)
target_include_directories(interpreter PUBLIC $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)
target_link_libraries(interpreter PRIVATE elementwise_program op_impls interpreter_lower Halide::Runtime)

