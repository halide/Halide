# Generators
find_package(OpenGL)
if (OpenGL_FOUND)

    # Generators
    halide_add_generator(halide_blur_glsl.generator
                         SRCS halide_blur_glsl_generator.cpp)
    halide_add_aot_library(halide_blur_glsl
                           GENERATOR_TARGET halide_blur_glsl.generator
                           GENERATOR_ARGS target=host-opengl-debug)

    halide_add_generator(halide_ycc_glsl.generator
                         SRCS halide_ycc_glsl_generator.cpp)
    halide_add_aot_library(halide_ycc_glsl
                           GENERATOR_TARGET halide_ycc_glsl.generator
                           GENERATOR_ARGS target=host-opengl-debug)

    # Final executable
    add_executable(opengl_test opengl_test.cpp)
    halide_add_aot_library_dependency(opengl_test halide_blur_glsl)
    halide_add_aot_library_dependency(opengl_test halide_ycc_glsl)
    target_compile_options(opengl_test PRIVATE "-std=c++11")

    if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        target_link_libraries(opengl_test PRIVATE pthread dl X11 GL)
    endif()

endif()

