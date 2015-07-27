#Generators
halide_project(glsl_halide_blur "apps" halide_blur.cpp)
set(hb_header "${CMAKE_CURRENT_BINARY_DIR}/blur.h")
set(hb_obj "${CMAKE_CURRENT_BINARY_DIR}/blur.o")
halide_project(glsl_halide_ycc "apps" halide_ycc.cpp)
set(hycc_header "${CMAKE_CURRENT_BINARY_DIR}/ycc.h")
set(hycc_obj "${CMAKE_CURRENT_BINARY_DIR}/ycc.o")

# Final executable
add_executable(opengl_test opengl_test.cpp ${hb_header} ${hycc_header})
target_link_libraries(opengl_test PRIVATE Halide ${OPENGL_LIBRARIES}
                    "${hb_obj}" "${hycc_obj}")
if (NOT WIN32)
target_link_libraries(opengl_test PRIVATE pthread dl)
endif()
target_include_directories(opengl_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
set_target_properties(glsl_halide_blur glsl_halide_ycc opengl_test
                    PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                    "${CMAKE_CURRENT_BINARY_DIR}")


# FIXME: The environment variable HL_TARGET needs setting
add_custom_command(OUTPUT "${hb_header}" "${hb_obj}"
                 COMMAND glsl_halide_blur
                 COMMENT "Generating halide_blur files"
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                 )
add_custom_command(OUTPUT "${hycc_header}" "${hycc_obj}"
                 COMMAND glsl_halide_ycc
                 COMMENT "Generating halide_blur files"
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                )
