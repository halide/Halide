cmake_minimum_required(VERSION 3.22)

# tip: uncomment this line to get better debugging information if find_library() fails
# set(CMAKE_FIND_DEBUG_MODE TRUE)

if (EXISTS "$ENV{HL_WEBGPU_NATIVE_LIB}")
  set(Halide_WebGPU_NATIVE_LIB "$ENV{HL_WEBGPU_NATIVE_LIB}"
      CACHE FILEPATH "")
endif ()

find_library(Halide_WebGPU_NATIVE_LIB NAMES webgpu_dawn wgpu)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  Halide_WebGPU
  REQUIRED_VARS Halide_WebGPU_NATIVE_LIB
  HANDLE_COMPONENTS
)

if (Halide_WebGPU_NATIVE_LIB AND NOT TARGET Halide::WebGPU)
  add_library(Halide::WebGPU UNKNOWN IMPORTED)
  set_target_properties(
    Halide::WebGPU
    PROPERTIES
    IMPORTED_LOCATION "${Halide_WebGPU_NATIVE_LIB}"
  )
endif ()
