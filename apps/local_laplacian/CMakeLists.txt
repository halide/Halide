# Generator
halide_add_generator(local_laplacian.generator
                     SRCS local_laplacian_generator.cpp)
halide_add_aot_library(local_laplacian
                       GENERATOR_TARGET local_laplacian.generator
                       GENERATOR_ARGS target=host)

# Final executable
add_executable(ll_process process.cpp)
halide_add_aot_library_dependency(ll_process local_laplacian)
target_link_libraries(ll_process PRIVATE ${PNG_LIBRARIES})
target_include_directories(ll_process PRIVATE "${PNG_INCLUDE_DIRS}")
target_compile_definitions(ll_process PRIVATE ${PNG_DEFINITIONS})
target_compile_options(ll_process PRIVATE "-std=c++11")

