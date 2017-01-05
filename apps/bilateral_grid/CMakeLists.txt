# Generator
halide_add_generator(bilateral_grid.generator
                     SRCS bilateral_grid_generator.cpp)
halide_add_aot_library(bilateral_grid
                       GENERATOR_TARGET bilateral_grid.generator
                       GENERATOR_ARGS target=host)

# Final executable
add_executable(filter filter.cpp)
halide_add_aot_library_dependency(filter bilateral_grid)
target_link_libraries(filter PRIVATE ${PNG_LIBRARIES})
target_include_directories(filter PRIVATE "${PNG_INCLUDE_DIRS}")
target_compile_definitions(filter PRIVATE ${PNG_DEFINITIONS})
target_compile_options(filter PRIVATE "-std=c++11")
