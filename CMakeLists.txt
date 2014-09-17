
project(Halide)
cmake_minimum_required(VERSION 2.8.7)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(LLVM_BIN "llvm3.2/bin/Debug/"
  CACHE PATH "Path to LLVM bin directory")
set(LLVM_LIB "llvm3.2/lib/Debug/"
  CACHE PATH "Path to LLVM lib directory")
set(LLVM_INCLUDE "llvm3.2/include/"
  CACHE PATH "Path to LLVM include directory")
set(LLVM_VERSION "32" CACHE STRING "First two digits of LLVM version (omitting '.')")

#message(STATUS "Gathering LLVM information")
#execute_process(
#  COMMAND ${LLVM_CONFIG} --components
#  OUTPUT_VARIABLE LLVM_COMPONENTS)
#message(STATUS "LLVM_COMPONENTS: ${LLVM_COMPONENTS}")

#execute_process(
#  COMMAND ${LLVM_CONFIG} --version
#  OUTPUT_VARIABLE LLVM_VERSION)
#message(STATUS "LLVM_VERSION: ${LLVM_VERSION}")

#execute_process(
#  COMMAND ${LLVM_CONFIG} --bindir
#  OUTPUT_VARIABLE LLVM_BINDIR)
#message(STATUS "LLVM_BINDIR: ${LLVM_BINDIR}")

#execute_process(
#  COMMAND ${LLVM_CONFIG} --libdir
#  OUTPUT_VARIABLE LLVM_LIBDIR)
#message(STATUS "LLVM_LIBDIR: ${LLVM_LIBDIR}")

# llvm_config doesn't always point to the correct include
# directory. We haven't figured out why yet.
#execute_process(
#  COMMAND ${LLVM_CONFIG} --cppflags
#  OUTPUT_VARIABLE LLVM_CXX_FLAGS)
#message(STATUS "LLVM_CXX_FLAGS: ${LLVM_CXX_FLAGS}")

#execute_process(
#  COMMAND ${CLANG_PATH} --version
#  OUTPUT_VARIABLE CLANG_VERSION)
#message(STATUS "CLANG_VERSION: ${CLANG_VERSION}")

set(OPTIMIZE "-O3"
  CACHE STRING "Optimization level")

#if (${LLVM_COMPONENTS} MATCHES x86)
#  set(WITH_X86 ON)
#else()
#  set(WITH_X86 OFF)
#endif()

file(TO_NATIVE_PATH "${LLVM_BIN}/llvm-as${CMAKE_EXECUTABLE_SUFFIX}" LLVM_AS)
file(TO_NATIVE_PATH "${LLVM_BIN}/llvm-nm${CMAKE_EXECUTABLE_SUFFIX}" LLVM_NM)
file(TO_NATIVE_PATH "${LLVM_BIN}/clang${CMAKE_EXECUTABLE_SUFFIX}" CLANG)

message(${CLANG})

option(TARGET_NATIVE_CLIENT "Include Native Client" ON)
option(TARGET_X86 "Include x86 target" ON)
option(TARGET_ARM "Include ARM target" ON)
option(TARGET_PTX "Include PTX target" ON)
option(TARGET_OPENCL "Include OpenCL-C target" ON)
option(TARGET_OPENGL "Include OpenGL/GLSL target" ON)
option(HALIDE_SHARED_LIBRARY "Build as a shared library" ON)

set(CAT cat)
if(WIN32)
  set(CAT type)
endif()

set (PROJECT_LIBS )
if (WIN32)
  list(APPEND PROJECT_LIBS Kernel32)
  add_definitions("/wd4244 /wd4267 /wd4800 /wd4996 /wd4305 /wd4146")
endif()

function(halide_project name folder)
  add_executable("${name}" ${ARGN})
  target_link_libraries("${name}" Halide ${PROJECT_LIBS})
  set_target_properties("${name}" PROPERTIES FOLDER "${folder}")
  if (WIN32)
    set_target_properties("${name}" PROPERTIES LINK_FLAGS "/STACK:8388608,1048576")
  endif()
endfunction(halide_project)

add_subdirectory(src)
add_subdirectory(test)

# The apps and tutorials don't really compile on windows yet
if (!WIN32)
  add_subdirectory(apps)
  add_subdirectory(tutorial)
endif()
