project(Halide)
cmake_minimum_required(VERSION 2.8.12)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(NOT DEFINED LLVM_CONFIG)
    set(LLVM_CONFIG "llvm-config")
endif()
if(NOT DEFINED CLANG_PATH)
    set(CLANG_PATH "clang")
endif()

function(get_llvm_info key output)
    execute_process(
        COMMAND ${LLVM_CONFIG} --${key}
        OUTPUT_STRIP_TRAILING_WHITESPACE
        OUTPUT_VARIABLE ${output})
    set(${output} ${${output}} PARENT_SCOPE)
endfunction()

message(STATUS "Gathering LLVM information")

get_llvm_info("components" LLVM_COMPONENTS)
if("${LLVM_COMPONENTS}" STREQUAL "")
    message(FATAL_ERROR
        "Could not find llvm-config. You can pass -DLLVM_CONFIG=path_to_llvm_config"
        " to use a specific version.")
endif()
message(STATUS "LLVM_COMPONENTS: ${LLVM_COMPONENTS}")

get_llvm_info("version" LLVM_VERSION)
string(REGEX REPLACE "\\." "" LLVM_VERSION ${LLVM_VERSION})
string(SUBSTRING ${LLVM_VERSION} 0 2 LLVM_VERSION)
message(STATUS "LLVM_VERSION: ${LLVM_VERSION}")

get_llvm_info("includedir" LLVM_INCLUDE)
message(STATUS "LLVM_INCLUDE: ${LLVM_INCLUDE}")

get_llvm_info("libdir" LLVM_LIB)
message(STATUS "LLVM_LIB: ${LLVM_LIB}")

get_llvm_info("bindir" LLVM_BINDIR)
message(STATUS "LLVM_BINDIR: ${LLVM_BINDIR}")

get_llvm_info("libs" LLVM_LIBS)

get_llvm_info("cppflags" LLVM_CXX_FLAGS)
message(STATUS "LLVM_CXX_FLAGS: ${LLVM_CXX_FLAGS}")

execute_process(
    COMMAND ${CLANG_PATH} --version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE CLANG_VERSION)
message(STATUS "CLANG_VERSION: ${CLANG_VERSION}")
if("${CLANG_VERSION}" STREQUAL "")
    message(FATAL_ERROR
        "Could not find clang. You can pass -DCLANG_PATH=path_to_clang"
        " to use a specific version.")
endif()

set(OPTIMIZE "-O3"
  CACHE STRING "Optimization level")

#if (${LLVM_COMPONENTS} MATCHES x86)
#  set(WITH_X86 ON)
#else()
#  set(WITH_X86 OFF)
#endif()

file(TO_NATIVE_PATH "${LLVM_BINDIR}/llvm-as${CMAKE_EXECUTABLE_SUFFIX}" LLVM_AS)
file(TO_NATIVE_PATH "${LLVM_BINDIR}/llvm-nm${CMAKE_EXECUTABLE_SUFFIX}" LLVM_NM)
file(TO_NATIVE_PATH "${LLVM_BINDIR}/clang${CMAKE_EXECUTABLE_SUFFIX}" CLANG)

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

set(PROJECT_LIBS ${LLVM_LIBS} z pthread tinfo dl png)

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
IF(CMAKE_SYSTEM_NAME STREQUAL Linux)
    add_subdirectory(apps)
    add_subdirectory(tutorial)
endif()
