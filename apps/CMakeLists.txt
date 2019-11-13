include_directories("${CMAKE_BINARY_DIR}/include")
include_directories("${HALIDE_TOOLS_DIR}")

if (MSVC)
    # Sample apps, so we can disable some of the more pedantic warnings
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_compile_options(/wd4244) # conversion from 'type1' to 'type2', possible loss of data.
endif()

add_subdirectory(autoscheduler)
add_subdirectory(bilateral_grid)
add_subdirectory(blur)
add_subdirectory(c_backend)
add_subdirectory(camera_pipe)
add_subdirectory(conv_layer)
add_subdirectory(glsl)
add_subdirectory(interpolate)
add_subdirectory(lens_blur)
add_subdirectory(linear_algebra)
add_subdirectory(linear_blur)
add_subdirectory(local_laplacian)
add_subdirectory(nl_means)
add_subdirectory(resize)
add_subdirectory(stencil_chain)

# Don't add this one; it's deliberately standalone
# add_subdirectory(wavelet)
