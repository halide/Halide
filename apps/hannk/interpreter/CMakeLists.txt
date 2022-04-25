
add_library(elementwise_program STATIC
            elementwise_program.cpp)
target_include_directories(elementwise_program PUBLIC $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)
target_link_libraries(elementwise_program PRIVATE Halide::Runtime)

add_library(interpreter_lower STATIC
            lower.cpp)
target_include_directories(interpreter_lower PUBLIC $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)
target_link_libraries(interpreter_lower PRIVATE Halide::Runtime)

add_library(interpreter STATIC
            allocation_planner.cpp
            interpreter.cpp
            interval.cpp
            model.cpp
            ops.cpp
            tensor.cpp
            transforms.cpp)
target_include_directories(interpreter PUBLIC $<BUILD_INTERFACE:${hannk_SOURCE_DIR}>)
target_link_libraries(interpreter PRIVATE elementwise_program halide_op_implementations interpreter_lower Halide::Runtime)

foreach (LIB IN ITEMS
            elementwise_program
            interpreter_lower
            interpreter)
    target_compile_options(${LIB}
                           PRIVATE
                           $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Woverloaded-virtual>
                           $<$<CXX_COMPILER_ID:GNU>:-Wsuggest-override>
                           $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Winconsistent-missing-destructor-override>
                           $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Winconsistent-missing-override>)
endforeach ()
