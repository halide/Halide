# Generator
halide_add_generator(lens_blur.generator
                     SRCS lens_blur_generator.cpp)
halide_add_aot_library(lens_blur
                       GENERATOR_NAME lens_blur
                       GENERATOR_TARGET lens_blur.generator
                       GENERATOR_ARGS target=host auto_schedule=false
                       GENERATED_FUNCTION lens_blur)
halide_add_aot_library(lens_blur_auto_schedule
                       GENERATOR_NAME lens_blur
                       GENERATOR_TARGET lens_blur.generator
                       GENERATOR_ARGS target=host auto_schedule=true
                       GENERATED_FUNCTION lens_blur_auto_schedule)

# Final executable
add_executable(lensblur_process process.cpp)
halide_add_aot_library_dependency(lensblur_process lens_blur)
halide_add_aot_library_dependency(lensblur_process lens_blur_auto_schedule)
halide_use_image_io(lensblur_process)
target_compile_options(lensblur_process PRIVATE "-std=c++11")
