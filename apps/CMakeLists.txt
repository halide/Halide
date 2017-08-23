include_directories ("${CMAKE_CURRENT_LIST_DIR}/support")
# The support header file depends on Halide.h
include_directories("${CMAKE_BINARY_DIR}/include")
include_directories("${CMAKE_SOURCE_DIR}/tools")

add_subdirectory(bilateral_grid)
add_subdirectory(blur)
add_subdirectory(c_backend)
add_subdirectory(camera_pipe)
#add_subdirectory(HelloAndroid)
add_subdirectory(glsl)
add_subdirectory(interpolate)
add_subdirectory(local_laplacian)
add_subdirectory(linear_algebra)
add_subdirectory(lens_blur)
add_subdirectory(nl_means)
add_subdirectory(conv_layer)

# Don't add this one; it's deliberately standalone
# add_subdirectory(wavelet)
