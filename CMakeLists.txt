
project(Halide)
cmake_minimum_required(VERSION 3.2)

set(HALIDE_BASE_DIR   "${CMAKE_CURRENT_SOURCE_DIR}")

set(CPACK_PACKAGE_VENDOR "Halide")
set(CPACK_RESOURCE_FILE_LICENSE "${HALIDE_BASE_DIR}/LICENSE.txt")
set(CPACK_MONOLITHIC_INSTALL OFF)
if (WIN32)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()
# Remove this to get package names that are formatted as
# ${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_SYSTEM_NAME}.
set(CPACK_PACKAGE_FILE_NAME "Halide" CACHE STRING "Name of package created by distrib target")
include(CPack)

find_package(Threads QUIET)

if("${HALIDE_REQUIRE_LLVM_VERSION}" STREQUAL "")
  # Find any version present.
  find_package(LLVM REQUIRED CONFIG)
else()
  # Find a specific version.
  string(SUBSTRING "${HALIDE_REQUIRE_LLVM_VERSION}" 0 1 MAJOR)
  string(SUBSTRING "${HALIDE_REQUIRE_LLVM_VERSION}" 1 1 MINOR)
  message("Looking for LLVM version ${MAJOR}.${MINOR}")
  find_package(LLVM "${MAJOR}.${MINOR}" REQUIRED CONFIG)
  if(NOT "${LLVM_VERSION_MAJOR}${LLVM_VERSION_MINOR}" STREQUAL "${MAJOR}${MINOR}")
    message(FATAL_ERROR "LLVM version error: required ${MAJOR}${MINOR} but found ${LLVM_VERSION_MAJOR}${LLVM_VERSION_MINOR}")
  endif()
endif()

# Notify the user what paths and LLVM version we are using
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

if (MSVC AND NOT CMAKE_CONFIGURATION_TYPES)
  # LLVM5.x on Windows can include "$(Configuration)" in the path;
  # fix this so we can use the paths right away.
  string(REPLACE "$(Configuration)" ${CMAKE_BUILD_TYPE} LLVM_TOOLS_BINARY_DIR "${LLVM_TOOLS_BINARY_DIR}")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  set(CMAKE_OBJECT_PATH_MAX 260)
  message("Windows: setting CMAKE_OBJECT_PATH_MAX to ${CMAKE_OBJECT_PATH_MAX}")
endif()

# Require C++11 for everything.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_MACOSX_RPATH ON)

# Export all symbols
SET(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(LLVM_VERSION "${LLVM_VERSION_MAJOR}${LLVM_VERSION_MINOR}")

file(TO_NATIVE_PATH "${LLVM_TOOLS_BINARY_DIR}/llvm-as${CMAKE_EXECUTABLE_SUFFIX}" LLVM_AS)
file(TO_NATIVE_PATH "${LLVM_TOOLS_BINARY_DIR}/llvm-nm${CMAKE_EXECUTABLE_SUFFIX}" LLVM_NM)
file(TO_NATIVE_PATH "${LLVM_TOOLS_BINARY_DIR}/clang${CMAKE_EXECUTABLE_SUFFIX}" CLANG)
file(TO_NATIVE_PATH "${LLVM_TOOLS_BINARY_DIR}/llvm-config${CMAKE_EXECUTABLE_SUFFIX}" LLVM_CONFIG)

# LLVM doesn't appear to expose --system-libs via its CMake interface,
# so we must shell out to llvm-config to find this info
execute_process(COMMAND ${LLVM_CONFIG} --system-libs --link-static OUTPUT_VARIABLE HALIDE_SYSTEM_LIBS_RAW)
string(STRIP "${HALIDE_SYSTEM_LIBS_RAW}" HALIDE_SYSTEM_LIBS_RAW)  # strip whitespace from start & end
string(REPLACE " " ";" HALIDE_SYSTEM_LIBS "${HALIDE_SYSTEM_LIBS_RAW}")  # convert into a list
if("${HALIDE_SYSTEM_LIBS}" STREQUAL "")
  # It's theoretically possible that this could be legitimately empty,
  # but in practice that doesn't really happen, so we'll assume it means we
  # aren't configured correctly.
  message(WARNING "'llvm-config --system-libs --link-static' is empty; this is possibly wrong.")
endif()

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
  if (MSVC)
    # LLVM5.x on Windows can include "$(Configuration)" in the path;
    # fix this so we can use the paths right away.
    string(REPLACE "$(Configuration)" ${CMAKE_BUILD_TYPE} TOOL_PATH "${TOOL_PATH}")
  endif()
  if (NOT EXISTS "${TOOL_PATH}")
    message(FATAL_ERROR "Tool ${NAME} not found at ${TOOL_PATH}")
  endif()
  message(STATUS "Using ${NAME} at ${TOOL_PATH}")
endfunction()

# Check LLVM tools exist
check_tool_exists(llvm-as "${LLVM_AS}")
check_tool_exists(llvm-nm "${LLVM_NM}")
check_tool_exists(clang "${CLANG}")

# Check reported LLVM version
if (NOT "${LLVM_VERSION}" MATCHES "^[0-9][0-9]$")
  message(FATAL_ERROR "LLVM_VERSION not specified correctly. Must be <major><minor> E.g. LLVM 7.0 is \"70\"")
endif()
if (LLVM_VERSION LESS 60)
  message(FATAL_ERROR "LLVM version must be 6.0 or newer")
endif()

function(check_llvm_target TARGET HAS_TARGET)
  set(${HAS_TARGET} OFF PARENT_SCOPE)
  set(_llvm_required_version ${LLVM_VERSION})
  if (ARGV2)
    set(_llvm_required_version ${ARGV2})
  endif()
  if (NOT LLVM_VERSION LESS _llvm_required_version)
    list(FIND LLVM_TARGETS_TO_BUILD ${TARGET} _found_target)
    if (_found_target GREATER -1)
      set(${HAS_TARGET} ON PARENT_SCOPE)
    else()
      set(${HAS_TARGET} OFF PARENT_SCOPE)
    endif()
  endif()
endfunction()

check_llvm_target(X86 WITH_X86)
check_llvm_target(ARM WITH_ARM)
check_llvm_target(AArch64 WITH_AARCH64)
check_llvm_target(Hexagon WITH_HEXAGON 40)
check_llvm_target(Mips WITH_MIPS)
check_llvm_target(PowerPC WITH_POWERPC)
check_llvm_target(NVPTX WITH_NVPTX)
# AMDGPU target is WIP
check_llvm_target(AMDGPU WITH_AMDGPU)

option(TARGET_NATIVE_CLIENT "Include Native Client" OFF)
option(TARGET_X86 "Include x86 target" ${WITH_X86})
option(TARGET_ARM "Include ARM target" ${WITH_ARM})
option(TARGET_AARCH64 "Include AARCH64 (arm64) target" ${WITH_AARCH64})
option(TARGET_HEXAGON "Include Hexagon target" ${WITH_HEXAGON})
option(TARGET_METAL "Include Metal target" ON)
option(TARGET_MIPS "Include MIPS target" ${WITH_MIPS})
option(TARGET_POWERPC "Include POWERPC target" ${WITH_POWERPC})
option(TARGET_PTX "Include PTX target" ${WITH_NVPTX})
option(TARGET_AMDGPU "Include AMDGPU target" ${WITH_AMDGPU})
option(TARGET_OPENCL "Include OpenCL-C target" ON)
option(TARGET_OPENGL "Include OpenGL/GLSL target" ON)
option(TARGET_OPENGLCOMPUTE "Include OpenGLCompute target" ON)
option(TARGET_D3D12COMPUTE "Include Direct3D 12 Compute target" ON)
option(HALIDE_SHARED_LIBRARY "Build as a shared library" ON)
option(HALIDE_ENABLE_RTTI "Enable RTTI" ${LLVM_ENABLE_RTTI})

if (HALIDE_SHARED_LIBRARY)
  set(HALIDE_LIBRARY_TYPE SHARED)
else()
  set(HALIDE_LIBRARY_TYPE STATIC)
endif()

if (HALIDE_ENABLE_RTTI AND NOT LLVM_ENABLE_RTTI)
  message(FATAL_ERROR "Can't enable RTTI. LLVM was compiled without it")
endif()

function(halide_project name folder)
  add_executable("${name}" ${ARGN})
  if (MSVC)
    if(NOT HALIDE_ENABLE_RTTI)
      target_compile_options("${name}" PUBLIC "/GR-")
    endif()
  else()
    if (NOT HALIDE_ENABLE_RTTI)
      target_compile_options("${name}" PUBLIC "-fno-rtti")
    endif()
  endif()
  target_link_libraries("${name}" PRIVATE Halide ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
  target_include_directories("${name}" PRIVATE "${HALIDE_BASE_DIR}/src")
  target_include_directories("${name}" PRIVATE "${HALIDE_BASE_DIR}/tools")
  set_target_properties("${name}" PROPERTIES FOLDER "${folder}")
  if (MSVC)
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
  if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 5.1)
    add_compile_options(-Wsuggest-override)
  endif()
  if (WARNINGS_AS_ERRORS)
    add_compile_options(-Werror)
  endif()
else()
  add_compile_options(/W3)
  add_compile_options(/wd4018)  # disable "signed/unsigned mismatch"
  add_compile_options(/wd4503)  # disable "decorated name length exceeded, name was truncated"
  add_compile_options(/wd4267)  # disable "conversion from 'size_t' to 'int', possible loss of data"
  add_compile_options(/wd4800)  # forcing value to bool 'true' or 'false' (performance warning)
  if (WARNINGS_AS_ERRORS)
    add_compile_options(/WX)
  endif()
endif()

# These tools are needed by several subdirectories
add_executable(build_halide_h tools/build_halide_h.cpp)
add_executable(binary2cpp tools/binary2cpp.cpp)
if (MSVC)
  # disable irrelevant "POSIX name" warnings
  target_compile_options(build_halide_h PUBLIC /wd4996)
  target_compile_options(binary2cpp PUBLIC /wd4996)
endif()


# Look for OpenMP
find_package(OpenMP QUIET)
if (OPENMP_FOUND)
  message(STATUS "Found OpenMP")
endif()

# For in-tree builds, we need to set the input variables for halide.cmake
# to specific values, rather than relying on HALIDE_DISTRIB_DIR to be set correctly.
set(HALIDE_INCLUDE_DIR "${CMAKE_BINARY_DIR}/include")
set(HALIDE_TOOLS_DIR "${HALIDE_BASE_DIR}/tools")
set(HALIDE_COMPILER_LIB Halide)
set(HALIDE_DISTRIB_DIR "/bad-path")
include(halide.cmake)

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

function(define_test_group GROUP)
  if(TARGET "${GROUP}")
    message(FATAL_ERROR "Group ${GROUP} is already defined.")
  endif()
  add_custom_target("${GROUP}")
  set_target_properties("${GROUP}" PROPERTIES EXCLUDE_FROM_ALL TRUE)
endfunction()

define_test_group(build_tests)
define_test_group(run_tests)

# Make a target that aggregates a number of other targets;
# this can be used to group builds (e.g. "build_tests")
# or to execute targets (by adding executable custom-commands);
# the latter is used to execute test targets.
#
# Note that the target will be excluded from the "all" target, so it won't
# be built by "make all" by default.
#
# TODO: this is intended to eventually replicate all of the interesting test targets
# from our Make build, but not all are implemented yet:
# TODO(srj): add test_aotcpp_generators support
# TODO(srj): add test_valgrind variant
# TODO(srj): add test_avx512 variant
# TODO(srj): add test_bazel variant
# TODO(srj): add test_python variant
# TODO(srj): add test_apps variant
function(add_test TARGET)
  set(options EXPECT_FAILURE)
  set(oneValueArgs WORKING_DIRECTORY)
  set(multiValueArgs GROUPS)
  cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
  foreach(GROUP ${args_GROUPS})
    if(NOT TARGET "${GROUP}")
      define_test_group("${GROUP}")
    endif()
  endforeach()

  set(BUILD_NAME "${TARGET}")
  # add_custom_target("${BUILD_NAME}" DEPENDS "${TARGET}")
  set_target_properties("${BUILD_NAME}" PROPERTIES EXCLUDE_FROM_ALL TRUE)
  add_dependencies(build_tests "${BUILD_NAME}")

  set(EXEC_NAME "run_${TARGET}")
  get_target_property(TARGET_TYPE "${TARGET}" TYPE)
  if("${TARGET_TYPE}" STREQUAL "EXECUTABLE")
    if(${args_EXPECT_FAILURE})
      set(COMMAND "${HALIDE_BASE_DIR}/test/common/expect_failure.sh" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TARGET}")
    else()
      set(COMMAND "${TARGET}")
    endif()
    add_custom_target("${EXEC_NAME}"
                     COMMAND ${COMMAND}
                     WORKING_DIRECTORY ${args_WORKING_DIRECTORY}
                     COMMENT "Executing ${TARGET}"
                     DEPENDS "${TARGET}")
  elseif("${TARGET_TYPE}" STREQUAL "UTILITY")
    # It's probably a custom target or a group: just depend on it.
    add_custom_target("${EXEC_NAME}" DEPENDS "${TARGET}")
  else()
    message(FATAL_ERROR "add_test(): unsupported type ${TARGET_TYPE} for ${TARGET}")
  endif()
  set_target_properties("${EXEC_NAME}" PROPERTIES EXCLUDE_FROM_ALL TRUE)
  foreach(GROUP ${args_GROUPS})
    add_dependencies("${GROUP}" "${EXEC_NAME}")
  endforeach()

endfunction()

# Only groups that are defined in *this file* will have projects reliably
# defined for them in MSVC builds; pre-define the targets we want to be available
# here to ensure that happens.
define_test_group(test_auto_schedule)
define_test_group(test_correctness)
define_test_group(test_error)
define_test_group(test_generator)
define_test_group(test_opengl)
define_test_group(test_performance)
define_test_group(test_tutorial)
define_test_group(test_warning)

add_subdirectory(src)
option(WITH_TESTS "Build tests" ON)
if (WITH_TESTS)
  message(STATUS "Building tests enabled")
  add_subdirectory(test)
else()
  message(STATUS "Building tests disabled")
endif()

option(WITH_APPS "Build apps" ON)
if (WITH_APPS)
  message(STATUS "Building apps enabled")
  add_subdirectory(apps)
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

  configure_file(${HALIDE_BASE_DIR}/Doxyfile.in ${CMAKE_BINARY_DIR}/Doxyfile @ONLY)
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

# ------------------------------------------------
# install

file(GLOB FILES "${CMAKE_BINARY_DIR}/include/HalideRuntime*.h")
install(FILES
        "${CMAKE_BINARY_DIR}/include/Halide.h"
        "${CMAKE_BINARY_DIR}/include/HalideBuffer.h"
        ${FILES}
        DESTINATION include
        )

install(DIRECTORY tutorial
        DESTINATION tutorial
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.sh"
        PATTERN "*.gif"
        PATTERN "*.jpg"
        PATTERN "*.mp4"
        PATTERN "*.png")

# ---- Tools
foreach(F mex_halide.m
          GenGen.cpp
          RunGen.h
          RunGenMain.cpp
          RunGenStubs.cpp
          halide_benchmark.h
          halide_image.h
          halide_image_io.h
          halide_image_info.h
          halide_malloc_trace.h
          halide_trace_config.h)
  install(FILES "${HALIDE_BASE_DIR}/tools/${F}"
          DESTINATION tools)
endforeach()

# ---- README
file(GLOB FILES "${HALIDE_BASE_DIR}/*.md")
install(FILES ${FILES}
        DESTINATION .)

# ---- Bazel
file(GLOB FILES "${HALIDE_BASE_DIR}/bazel/*")
install(FILES ${FILES}
        DESTINATION .)

# ---- CMake
file(GLOB FILES "${HALIDE_BASE_DIR}/*.cmake")
install(FILES ${FILES}
        DESTINATION .)

# ---- halide_config
file(GLOB FILES "${HALIDE_BASE_DIR}/tools/halide_config.*.tpl")
foreach(F ${FILES})
  get_filename_component(FNAME "${F}" NAME)  # Extract filename
  string(REGEX REPLACE "\\.tpl$" "" FNAME "${FNAME}")  # Strip .tpl extension
  configure_file("${F}" "${CMAKE_BINARY_DIR}/${FNAME}" @ONLY)
  install(FILES "${CMAKE_BINARY_DIR}/${FNAME}"
          DESTINATION .)
endforeach()

if (NOT WIN32)
  set(CPACK_PACKAGE_EXT "tar.gz")
  set(DISTRIB_PACKAGE_EXT "tgz")
else()
  set(CPACK_PACKAGE_EXT "zip")
  set(DISTRIB_PACKAGE_EXT ${CPACK_PACKAGE_EXT})
endif()

add_custom_target(distrib
  COMMAND cmake --build . --config $<CONFIG> --target package
  COMMAND cmake -E echo "${CPACK_PACKAGE_VENDOR}.${CPACK_PACKAGE_EXT} \"=>\" ${CPACK_PACKAGE_VENDOR}.${DISTRIB_PACKAGE_EXT}"
  COMMAND cmake -E rename "${CPACK_PACKAGE_VENDOR}.${CPACK_PACKAGE_EXT}" "${CPACK_PACKAGE_VENDOR}.${DISTRIB_PACKAGE_EXT}"
  BYPRODUCTS "${CPACK_PACKAGE_VENDOR}.${DISTRIB_PACKAGE_EXT}"
  USES_TERMINAL)
