#Generators
find_package(OpenGL)
if (OpenGL_FOUND)

halide_project(glsl_halide_blur "apps" halide_blur.cpp)
set(hb_header "${CMAKE_CURRENT_BINARY_DIR}/blur.h")
set(hb_lib "${CMAKE_CURRENT_BINARY_DIR}/blur${CMAKE_STATIC_LIBRARY_SUFFIX}")
halide_project(glsl_halide_ycc "apps" halide_ycc.cpp)
set(hycc_header "${CMAKE_CURRENT_BINARY_DIR}/ycc.h")
set(hycc_lib "${CMAKE_CURRENT_BINARY_DIR}/ycc${CMAKE_STATIC_LIBRARY_SUFFIX}")

# Final executable
add_executable(opengl_test opengl_test.cpp ${hb_header} ${hycc_header})
target_link_libraries(opengl_test PRIVATE Halide ${OPENGL_LIBRARIES}
                    "${hb_lib}" "${hycc_lib}")
IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
target_link_libraries(opengl_test PRIVATE pthread dl X11 GL)
endif()
target_include_directories(opengl_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# FIXME: The environment variable HL_TARGET needs setting
add_custom_command(OUTPUT "${hb_header}" "${hb_lib}"
                 COMMAND glsl_halide_blur
                 COMMENT "Generating halide_blur files"
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                 )
add_custom_command(OUTPUT "${hycc_header}" "${hycc_lib}"
                 COMMAND glsl_halide_ycc
                 COMMENT "Generating halide_blur files"
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                )
endif()

