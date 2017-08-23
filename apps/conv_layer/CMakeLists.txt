# Generator
halide_add_generator(conv_layer.generator
                     SRCS conv_layer_generator.cpp)
halide_add_aot_library(conv_layer
                       GENERATOR_TARGET conv_layer.generator
                       GENERATOR_ARGS target=host auto_schedule=false
                       GENERATED_FUNCTION conv_layer)
halide_add_aot_library(conv_layer_auto_schedule
                       GENERATOR_NAME conv_layer
                       GENERATOR_TARGET conv_layer.generator
                       GENERATOR_ARGS target=host auto_schedule=true
                       GENERATED_FUNCTION conv_layer_auto_schedule)

# Final executable
add_executable(conv_layer_process process.cpp)
halide_add_aot_library_dependency(conv_layer_process conv_layer)
halide_add_aot_library_dependency(conv_layer_process conv_layer_auto_schedule)
halide_use_image_io(conv_layer_process)
target_compile_options(conv_layer_process PRIVATE "-std=c++11")
