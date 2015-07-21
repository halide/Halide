
project(Halide)
cmake_minimum_required(VERSION 2.8.12)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(LLVM_BIN "llvm3.6/bin/Debug/"
  CACHE PATH "Path to LLVM bin directory")
set(LLVM_LIB "llvm3.6/lib/Debug/"
  CACHE PATH "Path to LLVM lib directory")
set(LLVM_INCLUDE "llvm3.6/include/"
  CACHE PATH "Path to LLVM include directory")
set(LLVM_VERSION "36" CACHE STRING "First two digits of LLVM version (omitting '.')")

file(TO_NATIVE_PATH "${LLVM_BIN}/llvm-as${CMAKE_EXECUTABLE_SUFFIX}" LLVM_AS)
file(TO_NATIVE_PATH "${LLVM_BIN}/llvm-nm${CMAKE_EXECUTABLE_SUFFIX}" LLVM_NM)
file(TO_NATIVE_PATH "${LLVM_BIN}/clang${CMAKE_EXECUTABLE_SUFFIX}" CLANG)

# Check LLVM
function(check_dir VARNAME PATH)
  if (NOT IS_ABSOLUTE "${PATH}")
    message(FATAL_ERROR "\"${PATH}\" (${VARNAME}) must be an absolute path")
  endif()
  if (NOT IS_DIRECTORY "${PATH}")
    message(FATAL_ERROR "\"${PATH}\" (${VARNAME}) must be a directory")
  endif()
endfunction()
function(check_tool_exists NAME PATH)
  # Need to convert to CMake path so that backslashes don't get
  # interpreted as an escape.
  file(TO_CMAKE_PATH "${PATH}" TOOL_PATH)
  if (NOT EXISTS "${TOOL_PATH}")
    message(FATAL_ERROR "Tool ${NAME} not found at ${TOOL_PATH}")
  endif()
  message(STATUS "Using ${NAME} at ${TOOL_PATH}")
endfunction()

# Notify the user what paths and LLVM version we are using
message(STATUS "LLVM_BIN:${LLVM_BIN}")
message(STATUS "LLVM_LIB:${LLVM_LIB}")
message(STATUS "LLVM_INCLUDE:${LLVM_INCLUDE}")
message(STATUS "LLVM_VERSION:${LLVM_VERSION}")

# Check path to LLVM directories exist
check_dir(LLVM_BIN "${LLVM_BIN}")
check_dir(LLVM_LIB "${LLVM_LIB}")
# Include might be a list of directories if LLVM was built out of source
foreach( include_dir ${LLVM_INCLUDE})
  check_dir(LLVM_INCLUDE "${include_dir}")
endforeach()

# Check LLVM tools exist
check_tool_exists(llvm-as "${LLVM_AS}")
check_tool_exists(llvm-nm "${LLVM_NM}")
check_tool_exists(clang "${CLANG}")

# Check reported LLVM version
if (NOT "${LLVM_VERSION}" MATCHES "^[0-9][0-9]$")
  message(FATAL_ERROR "LLVM_VERSION not specified correctly. Must be <major><minor> E.g. LLVM 3.5 is \"35\"")
endif()
if (LLVM_VERSION LESS 35)
  message(FATAL_ERROR "LLVM version must be 3.5 or newer")
endif()

option(TARGET_NATIVE_CLIENT "Include Native Client" OFF)
option(TARGET_X86 "Include x86 target" ON)
option(TARGET_ARM "Include ARM target" ON)
option(TARGET_AARCH64 "Include AARCH64 (arm64) target" ON)
option(TARGET_MIPS "Include MIPS target" ON)
option(TARGET_PTX "Include PTX target" ON)
option(TARGET_OPENCL "Include OpenCL-C target" ON)
option(TARGET_OPENGL "Include OpenGL/GLSL target" ON)
option(TARGET_OPENGLCOMPUTE "Include OpenGLCompute target" ON)
option(HALIDE_SHARED_LIBRARY "Build as a shared library" ON)

function(halide_project name folder)
  add_executable("${name}" ${ARGN})
  target_link_libraries("${name}" PRIVATE Halide)
  set_target_properties("${name}" PROPERTIES FOLDER "${folder}")
  if (WIN32)
    set_target_properties("${name}" PROPERTIES LINK_FLAGS "/STACK:8388608,1048576")
    target_link_libraries("${name}" PRIVATE Kernel32)
  endif()
endfunction(halide_project)

add_subdirectory(src)
option(WITH_TESTS "Build tests" ON)
if (WITH_TESTS)
  message(STATUS "Building tests enabled")
  add_subdirectory(test)
else()
  message(STATUS "Building tests disabled")
endif()

# Look for libpng. Some apps and tutorials depend on it
find_package(PNG)
option(WITH_APPS "Build apps" ON)
if (WITH_APPS)
  if (NOT WIN32)
    message(STATUS "Building apps enabled")
    add_subdirectory(apps)
  else()
    message(WARNING "Apps cannot be built under Windows")
  endif()
else()
  message(STATUS "Building apps disabled")
endif()
option(WITH_TUTORIALS "Build Tutorials" ON)
if (WITH_TUTORIALS)
  message(STATUS "Building tutorials enabled")
  add_subdirectory(tutorial)
else()
  message(STATUS "Building tutorials disabled")
endif()

option(WITH_DOCS "Enable building of documentation" OFF)
if (WITH_DOCS)
find_package(Doxygen)
  if (NOT DOXYGEN_FOUND)
    message(FATAL_ERROR "Could not find Doxygen. Either install it or set WITH_DOCS to OFF")
  endif()

  configure_file(${CMAKE_SOURCE_DIR}/Doxyfile.in ${CMAKE_BINARY_DIR}/Doxyfile @ONLY)
  # Note documentation is not built by default, the user needs to build the "doc" target
  add_custom_target(doc
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Building Doxygen documentation"
  )
endif()
