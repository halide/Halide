# FIXME: Renable by default once we fix the build
option(WITH_APPS "Build apps" OFF)

include_directories ("${CMAKE_BINARY_DIR}/include")
link_directories ("${LLVM_LIB}")
include_directories ("${CMAKE_CURRENT_LIST_DIR}/support")

if(WITH_APPS)
  add_subdirectory(bilateral_grid)
  add_subdirectory(blur)
  add_subdirectory(c_backend)
  add_subdirectory(camera_pipe)
  #add_subdirectory(HelloAndroid)
  #add_subdirectory(HelloNaCl)
  add_subdirectory(interpolate)
  add_subdirectory(local_laplacian)
  add_subdirectory(wavelet)
endif()
