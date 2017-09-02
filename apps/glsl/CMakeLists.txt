# Generators
find_package(OpenGL)
if (OpenGL_FOUND)

    # Generators
    halide_library(halide_blur_glsl
                   SRCS halide_blur_glsl_generator.cpp
                   HALIDE_TARGET_FEATURES opengl debug)

    halide_library(halide_ycc_glsl
                   SRCS halide_ycc_glsl_generator.cpp
                   HALIDE_TARGET_FEATURES opengl debug)

    # Final executable
    add_executable(opengl_test opengl_test.cpp)
    target_link_libraries(opengl_test PUBLIC halide_blur_glsl)
    target_link_libraries(opengl_test PUBLIC halide_ycc_glsl)

    if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        target_link_libraries(opengl_test PRIVATE pthread dl X11 GL)
    endif()

endif()

