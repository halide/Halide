# Generator
halide_add_generator(local_laplacian_fixed.generator
                     SRCS local_laplacian_fixed_generator.cpp)
halide_add_aot_library(local_laplacian_fixed
                       GENERATOR_TARGET local_laplacian_fixed.generator
                       GENERATOR_ARGS target=host)

# Final executable
add_executable(llf_process process.cpp)
halide_add_aot_library_dependency(llf_process local_laplacian_fixed)
target_link_libraries(llf_process PRIVATE ${PNG_LIBRARIES})
target_include_directories(llf_process PRIVATE "${PNG_INCLUDE_DIRS}")
target_compile_definitions(llf_process PRIVATE ${PNG_DEFINITIONS})
target_compile_options(llf_process PRIVATE "-std=c++11")

