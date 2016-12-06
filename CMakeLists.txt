
project(Halide)
cmake_minimum_required(VERSION 2.8.12)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(LLVM_BIN "llvm3.7/bin/Debug/"
  CACHE PATH "Path to LLVM bin directory")
set(LLVM_LIB "llvm3.7/lib/Debug/"
  CACHE PATH "Path to LLVM lib directory")
set(LLVM_INCLUDE "llvm3.7/include/"
  CACHE PATH "Path to LLVM include directory")
set(LLVM_VERSION "37" CACHE STRING "First two digits of LLVM version (omitting '.')")

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
  message(FATAL_ERROR "LLVM_VERSION not specified correctly. Must be <major><minor> E.g. LLVM 3.7 is \"37\"")
endif()
if (LLVM_VERSION LESS 37)
  message(FATAL_ERROR "LLVM version must be 3.7 or newer")
endif()

option(TARGET_X86 "Include x86 target" ON)
option(TARGET_ARM "Include ARM target" ON)
option(TARGET_AARCH64 "Include AARCH64 (arm64) target" ON)
# Hexagon requires LLVM 3.9 or later.
if (NOT(LLVM_VERSION LESS 39))
  option(TARGET_HEXAGON "Include Hexagon target" ON)
else()
  option(TARGET_HEXAGON "Include Hexagon target" OFF)
endif()
option(TARGET_METAL "Include Metal target" ON)
option(TARGET_MIPS "Include MIPS target" ON)
option(TARGET_POWERPC "Include POWERPC target" ON)
option(TARGET_PTX "Include PTX target" ON)
option(TARGET_OPENCL "Include OpenCL-C target" ON)
option(TARGET_OPENGL "Include OpenGL/GLSL target" ON)
option(TARGET_OPENGLCOMPUTE "Include OpenGLCompute target" ON)
option(HALIDE_SHARED_LIBRARY "Build as a shared library" ON)
option(HALIDE_ENABLE_RTTI "Build with RTTI enabled" OFF)

function(halide_project name folder)
  add_executable("${name}" ${ARGN})
  if (MSVC)
  else()
    target_compile_options("${name}" PUBLIC "-std=c++11") # Halide clients need C++11
    if(NOT HALIDE_ENABLE_RTTI)
      target_compile_options("${name}" PUBLIC "-fno-rtti")
    endif()
  endif()
  target_link_libraries("${name}" PRIVATE Halide)
  target_include_directories("${name}" PRIVATE "${CMAKE_SOURCE_DIR}/src")
  target_include_directories("${name}" PRIVATE "${CMAKE_SOURCE_DIR}/tools")
  set_target_properties("${name}" PROPERTIES FOLDER "${folder}")
  if (MSVC)
    # 4006: "already defined, second definition ignored"
    # 4088: "/FORCE used, image may not work"
    # (Note that MSVC apparently considers 4088 too important to allow us to ignore it;
    # I'm nevertheless leaving this here to document that we don't care about it.)
    set_target_properties(${name} PROPERTIES LINK_FLAGS "/ignore:4006 /ignore:4088")
    target_compile_definitions("${name}" PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries("${name}" PRIVATE Kernel32)
  endif()
endfunction(halide_project)

# Set warnings globally
option(WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
if (WARNINGS_AS_ERRORS)
    message(STATUS "WARNINGS_AS_ERRORS enabled")
else()
    message(STATUS "WARNINGS_AS_ERRORS disabled")
endif()

if (NOT MSVC)
    add_compile_options(-Wall
                        -Wno-unused-function
                        -Wcast-qual
                        -Woverloaded-virtual
                        -Wignored-qualifiers)
  if (WARNINGS_AS_ERRORS)
    add_compile_options(-Werror)
  endif()
else()
  add_compile_options(/W3)
  message(WARNING "WARNINGS_AS_ERROR is currently ignored for MSVC")
  # TODO: Uncomment below and remove above warning once we can build without warnings
  #if (WARNINGS_AS_ERRORS)
  #  add_compile_options(/WX)
  #endif()
endif()

# Look for OpenMP
find_package(OpenMP QUIET)
if (OPENMP_FOUND)
  message(STATUS "Found OpenMP")
endif()

# -----------------------------------------------------------------------------
# Option to enable/disable assertions
# -----------------------------------------------------------------------------
# Filter out definition of NDEBUG definition from the default build
# configuration flags.  # We will add this ourselves if we want to disable
# assertions.
# FIXME: Perhaps our own default ``cxx_flags_overrides.cmake`` file would be better?
foreach (build_config Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER ${build_config} upper_case_build_config)
  foreach (language CXX C)
    set(VAR_TO_MODIFY "CMAKE_${language}_FLAGS_${upper_case_build_config}")
    string(REGEX REPLACE "(^| )[/-]D *NDEBUG($| )"
                         " "
                         replacement
                         "${${VAR_TO_MODIFY}}"
          )
    #message("Original (${VAR_TO_MODIFY}) is ${${VAR_TO_MODIFY}} replacement is ${replacement}")
    set(${VAR_TO_MODIFY} "${replacement}" CACHE STRING "Default flags for ${build_config} configuration" FORCE)
  endforeach()
endforeach()

option(WITH_ASSERTIONS "Build with assertions enabled" ON)
if (WITH_ASSERTIONS)
  # NDEBUG was already removed.
  message(STATUS "Building with asserts")
else()
  # Note this definition doesn't appear in the cache variables.
  add_definitions(-DNDEBUG)
  message(STATUS "Building without asserts")
endif()

add_subdirectory(src)
add_subdirectory(tools)
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
    message(WARNING "Apps cannot be built under Windows using cmake. Try the makefile in an msys2 shell.")
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

option(WITH_UTILS "Build utils" ON)
if (WITH_UTILS)
  message(STATUS "Building utils enabled")
  add_subdirectory(util)
else()
  message(STATUS "Building utils disabled")
endif()
