include_directories ("${CMAKE_CURRENT_LIST_DIR}/support")
# The support header file depends on Halide.h
include_directories("${CMAKE_BINARY_DIR}/include")
include_directories("${CMAKE_SOURCE_DIR}/tools")

macro(add_if_have_libpng dirname)
  if (PNG_FOUND)
    add_subdirectory(${dirname})
  else()
    message(WARNING "Not building \"${dirname}\" because libpng could not be found")
  endif()
endmacro()

add_if_have_libpng(bilateral_grid)
add_subdirectory(blur)
add_subdirectory(c_backend)
add_if_have_libpng(camera_pipe)
#add_subdirectory(HelloAndroid)
add_subdirectory(glsl)
add_if_have_libpng(interpolate)
add_if_have_libpng(local_laplacian)

# Don't add this one; it's deliberately standalone
#add_subdirectory(wavelet)
