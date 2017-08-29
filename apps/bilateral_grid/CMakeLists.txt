# Generator
halide_add_generator(bilateral_grid.generator
                     SRCS bilateral_grid_generator.cpp)
halide_add_aot_library(bilateral_grid
                       GENERATOR_TARGET bilateral_grid.generator
                       GENERATOR_ARGS target=host auto_schedule=false
                       GENERATED_FUNCTION bilateral_grid)
halide_add_aot_library(bilateral_grid_auto_schedule
                       GENERATOR_NAME bilateral_grid
                       GENERATOR_TARGET bilateral_grid.generator
                       GENERATOR_ARGS target=host auto_schedule=true
                       GENERATED_FUNCTION bilateral_grid_auto_schedule)

# Final executable
add_executable(filter filter.cpp)
halide_add_aot_library_dependency(filter bilateral_grid)
halide_add_aot_library_dependency(filter bilateral_grid_auto_schedule)
halide_use_image_io(filter)
