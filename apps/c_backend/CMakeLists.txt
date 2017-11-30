# Generator(s)
halide_generator(pipeline.generator
                 SRCS pipeline_generator.cpp)
halide_library_from_generator(pipeline_c
                              GENERATOR pipeline.generator)
halide_library_from_generator(pipeline_native
                              GENERATOR pipeline.generator)

halide_generator(pipeline_cpp.generator
                 SRCS pipeline_cpp_generator.cpp)
halide_library_from_generator(pipeline_cpp_cpp
                              GENERATOR pipeline_cpp.generator
                              HALIDE_TARGET_FEATURES c_plus_plus_name_mangling)
halide_library_from_generator(pipeline_cpp_native
                              GENERATOR pipeline_cpp.generator
                              HALIDE_TARGET_FEATURES c_plus_plus_name_mangling)

# Final executable(s)
add_executable(run_c_backend_and_native run.cpp)
target_link_libraries(run_c_backend_and_native 
                      PUBLIC pipeline_native pipeline_c_cc)

add_executable(run_c_backend_and_native_cpp run_cpp.cpp)
target_link_libraries(run_c_backend_and_native_cpp 
                      PUBLIC pipeline_cpp_native pipeline_cpp_cpp_cc)






