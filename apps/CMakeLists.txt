option(WITH_APPS "Build apps" ON)

include_directories ("${CMAKE_CURRENT_LIST_DIR}/support")

macro(add_if_have_libpng dirname)
  if (PNG_FOUND)
    add_subdirectory(${dirname})
  else()
    message(WARNING "Not building \"${dirname}\" because libpng could not be found")
  endif()
endmacro()

if(WITH_APPS)
  add_if_have_libpng(bilateral_grid)
  add_subdirectory(blur)
  add_subdirectory(c_backend)
  add_subdirectory(camera_pipe)
  #add_subdirectory(HelloAndroid)
  #add_subdirectory(HelloNaCl)
  add_subdirectory(interpolate)
  # Disabled due to CMake error
  #add_subdirectory(local_laplacian)
  add_subdirectory(wavelet)
endif()
