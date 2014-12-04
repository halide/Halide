project(Halide)
cmake_minimum_required(VERSION 2.8.12)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if (NOT CMAKE_BUILD_TYPE MATCHES "Release")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

set(LLVM_CONFIG "llvm-config" CACHE PATH "Path to llvm-config executable")

function(get_llvm_info key output)
    execute_process(
        COMMAND ${LLVM_CONFIG} --${key}
        OUTPUT_STRIP_TRAILING_WHITESPACE
        OUTPUT_VARIABLE ${output})
    if("${${output}}" STREQUAL "")
        message(FATAL_ERROR
            "Could not find llvm-config. You can indicate its path via -DLLVM_CONFIG=path_to_llvm-config")
    endif()
    message(STATUS "${output}: ${${output}}")
    set(${output} ${${output}} PARENT_SCOPE)
endfunction()

message(STATUS "Gathering LLVM information")

get_llvm_info("components" LLVM_COMPONENTS)

get_llvm_info("version" LLVM_VERSION)
string(REGEX REPLACE "\\." "" LLVM_VERSION ${LLVM_VERSION})
string(SUBSTRING ${LLVM_VERSION} 0 2 LLVM_VERSION)
message(STATUS "LLVM_VERSION: ${LLVM_VERSION}")

get_llvm_info("includedir" LLVM_INCLUDE)
get_llvm_info("libdir" LLVM_LIB)
get_llvm_info("bindir" LLVM_BINDIR)
get_llvm_info("libs" LLVM_LIBS)
get_llvm_info("system-libs" LLVM_SYSTEM_LIBS)
get_llvm_info("cppflags" LLVM_CXX_FLAGS)

file(TO_NATIVE_PATH "${LLVM_BINDIR}/llvm-as${CMAKE_EXECUTABLE_SUFFIX}" LLVM_AS)
file(TO_NATIVE_PATH "${LLVM_BINDIR}/llvm-nm${CMAKE_EXECUTABLE_SUFFIX}" LLVM_NM)
file(TO_NATIVE_PATH "${LLVM_BINDIR}/clang${CMAKE_EXECUTABLE_SUFFIX}" CLANG)

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

set(PROJECT_LIBS ${LLVM_LIBS} ${LLVM_SYSTEM_LIBS})

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
IF(NOT CMAKE_SYSTEM_NAME MATCHES Windows)
    add_subdirectory(apps)
    add_subdirectory(tutorial)
endif()
