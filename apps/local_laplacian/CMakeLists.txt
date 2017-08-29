# Generator
halide_add_generator(local_laplacian.generator
                     SRCS local_laplacian_generator.cpp)
halide_add_aot_library(local_laplacian
                       GENERATOR_TARGET local_laplacian.generator
                       GENERATOR_ARGS target=host auto_schedule=false
                       GENERATED_FUNCTION local_laplacian)
halide_add_aot_library(local_laplacian_auto_schedule
                       GENERATOR_NAME local_laplacian
                       GENERATOR_TARGET local_laplacian.generator
                       GENERATOR_ARGS target=host auto_schedule=true
                       GENERATED_FUNCTION local_laplacian_auto_schedule)

# Final executable
add_executable(ll_process process.cpp)
halide_add_aot_library_dependency(ll_process local_laplacian)
halide_add_aot_library_dependency(ll_process local_laplacian_auto_schedule)
halide_use_image_io(ll_process)
