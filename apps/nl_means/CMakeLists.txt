# Generator
halide_add_generator(nl_means.generator
                     SRCS nl_means_generator.cpp)
halide_add_aot_library(nl_means
                       GENERATOR_TARGET nl_means.generator
                       GENERATOR_ARGS target=host auto_schedule=false
                       GENERATED_FUNCTION nl_means)
halide_add_aot_library(nl_means_auto_schedule
                       GENERATOR_TARGET nl_means.generator
                       GENERATOR_ARGS target=host auto_schedule=true
                       GENERATED_FUNCTION nl_means_auto_schedule)

# Final executable
add_executable(ll_process process.cpp)
halide_add_aot_library_dependency(ll_process nl_means)
halide_add_aot_library_dependency(ll_process nl_means_auto_schedule)
use_image_io(ll_process)
target_compile_options(ll_process PRIVATE "-std=c++11")
