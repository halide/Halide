cmake_minimum_required(VERSION 3.22)

# tip: uncomment this line to get better debugging information if find_library() fails
# set(CMAKE_FIND_DEBUG_MODE TRUE)

# ENV{HL_WEBGPU_NATIVE_LIB} is expected to be an absolute path
# to the actual Dawn shared library, e.g. "/path/to/libwebgpu_dawn.dylib".
# However, find_library() won't accept this format, and instead requires a
# directory-and-stripped-library name, e.g. "/path/to" and "webgpu_dawn".
# So, we'll do the necessary surgery.
if (DEFINED ENV{HL_WEBGPU_NATIVE_LIB})
  set(WEBGPU_NATIVE_LIB $ENV{HL_WEBGPU_NATIVE_LIB})
  cmake_path(GET WEBGPU_NATIVE_LIB PARENT_PATH WEBGPU_NATIVE_DIR)
  cmake_path(GET WEBGPU_NATIVE_LIB STEM WEBGPU_NATIVE_NAME)
  # strip the "lib" prefix from the stem (if any)
  string(REGEX REPLACE "^lib" "" WEBGPU_NATIVE_NAME "${WEBGPU_NATIVE_NAME}")
  set(WEBGPU_NATIVE_NAMES ${WEBGPU_NATIVE_NAME})
else ()
  # Just default to whatever search paths CMake wants to use,
  # and specify two common names for the NAMES part.
  set(WEBGPU_NATIVE_DIR "")
  set(WEBGPU_NATIVE_NAMES "webgpu_dawn;wgpu")
endif()

find_library(
  Halide_WebGPU_NATIVE_LIB
  NAMES ${WEBGPU_NATIVE_NAMES}
  HINTS ${WEBGPU_NATIVE_DIR}
)

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
