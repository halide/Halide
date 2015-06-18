
project(Halide)
cmake_minimum_required(VERSION 2.8.12)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Find LLVM
# Users can set LLVM_DIR to point to a directory containing LLVMConfig.cmake
message(STATUS "Looking for LLVM")
find_package(LLVM REQUIRED)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
if (LLVM_PACKAGE_VERSION  VERSION_LESS 3.5)
  message(FATAL_ERROR "Need LLVM >=3.5")
endif()

if (MSVC)
  # WORKAROUND: Read the location from an imported target to get LLVM's binary
  # directory. Using the ${LLVM_TOOLS_BINARY_DIR} variable is currently broken
  # when using Visual Studio (it contains the string $(Configuration) in the
  # path). We shouldn't read the location from the imported binary targets on
  # other platforms because if LLVM was built with the Autoconf/Makefile build
  # system they won't be available.
  get_target_property(LLVM_AS_PATH llvm-as LOCATION)
  get_filename_component(LLVM_BIN "${LLVM_AS_PATH}" DIRECTORY)
else()
  set(LLVM_BIN ${LLVM_TOOLS_BINARY_DIR})
endif()

# TODO: Remove these variables and use LLVM variables directly
message(STATUS "LLVM_BIN:${LLVM_BIN}")
set(LLVM_INCLUDE ${LLVM_INCLUDE_DIRS})
message(STATUS "LLVM_INCLUDE:${LLVM_INCLUDE}")
set(LLVM_VERSION ${LLVM_VERSION_MAJOR}${LLVM_VERSION_MINOR})
message(STATUS "LLVM_VERSION:${LLVM_VERSION}")

file(TO_NATIVE_PATH "${LLVM_BIN}/llvm-as${CMAKE_EXECUTABLE_SUFFIX}" LLVM_AS)
file(TO_NATIVE_PATH "${LLVM_BIN}/llvm-nm${CMAKE_EXECUTABLE_SUFFIX}" LLVM_NM)
file(TO_NATIVE_PATH "${LLVM_BIN}/clang${CMAKE_EXECUTABLE_SUFFIX}" CLANG)

# Check that LLVM binaries we depend on exist
function(check_tool_exists NAME PATH)
  # Need to convert to CMake path so that backslashes don't get
  # interpreted as an escape.
  file(TO_CMAKE_PATH "${PATH}" TOOL_PATH)
  if (NOT EXISTS ${TOOL_PATH})
    message(FATAL_ERROR "Tool ${NAME} not found at ${TOOL_PATH}")
  endif()
  message(STATUS "Using ${NAME} at ${TOOL_PATH}")
endfunction()
check_tool_exists(llvm-as ${LLVM_AS})
check_tool_exists(llvm-nm ${LLVM_NM})
check_tool_exists(clang ${CLANG})

option(TARGET_NATIVE_CLIENT "Include Native Client" OFF)
option(TARGET_X86 "Include x86 target" ON)
option(TARGET_ARM "Include ARM target" ON)
option(TARGET_AARCH64 "Include AARCH64 (arm64) target" ON)
option(TARGET_MIPS "Include MIPS target" ON)
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
  # FIXME: Do we really only want to supress the warnings here?
  add_definitions("/wd4305 /wd4146")
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
option(BUILD_TESTS "Build tests" ON)
if (BUILD_TESTS)
  add_subdirectory(test)
endif()

# The apps and tutorials don't really compile on windows yet
if (NOT WIN32)
  add_subdirectory(apps)
  add_subdirectory(tutorial)
else()
  message(STATUS "Building tutorials and apps disabled under windows")
endif()

option(WITH_DOCS "Enable building of documentation" OFF)
if (WITH_DOCS)
find_package(Doxygen)
  if (NOT DOXYGEN_FOUND)
    message(FATAL_ERROR "Could not find Doxygen. Either install it or set WITH_DOCS to OFF")
  endif()

  # FIXME: Remove suffix when Doxygen support is removed from the Makefile build system or when
  # when the Makefile build system is dropped entirely.
  # note the ``_cmake`` suffix is so we don't clobber the Doxyfile used by the Makefile build
  # system if we do an in source CMake build.
  configure_file(${CMAKE_SOURCE_DIR}/Doxyfile.in ${CMAKE_BINARY_DIR}/Doxyfile_cmake @ONLY)
  # Note documentation is not built by default, the user needs to build the "doc" target
  add_custom_target(doc
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile_cmake
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Building Doxygen documentation"
  )
endif()
