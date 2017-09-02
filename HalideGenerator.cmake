include(CMakeParseArguments)

cmake_minimum_required(VERSION 3.1.3)

# ----------------------- Public Functions. 
# These are all documented in README_cmake.md.
#
# Note that certain CMake variables may need to be set correctly to use these rules:
#
# - If you are using a Halide distribution, simply set HALIDE_DISTRIB_DIR
# to the path to the distrib directory. 
#
# - More complex usages (mainly, internal-to-Halide users) may, instead, set some combination
# of HALIDE_TOOLS_DIR, HALIDE_INCLUDE_DIR, and HALIDE_COMPILER_LIB. 
#

# Add the include paths and link dependencies for halide_image_io.
function(halide_use_image_io TARGET)
  foreach(PKG PNG JPEG)
    find_package(${PKG} QUIET)
    if(${PKG}_FOUND)
      target_compile_definitions(${TARGET} PRIVATE ${${PKG}_DEFINITIONS})
      target_include_directories(${TARGET} PRIVATE ${${PKG}_INCLUDE_DIRS})
      target_link_libraries(${TARGET} PRIVATE ${${PKG}_LIBRARIES})
    else()
      message(STATUS "${PKG} not found for ${TARGET}; compiling with -DHALIDE_NO_${PKG}")
      target_compile_definitions(${TARGET} PRIVATE -DHALIDE_NO_${PKG})
    endif()
  endforeach()
endfunction()

# Make a build target for a Generator.
function(halide_generator NAME)
  set(oneValueArgs GENERATOR_NAME)
  set(multiValueArgs SRCS DEPS INCLUDES)
  cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT ${NAME} MATCHES "^.*\\.generator$")
    message(FATAL_ERROR "halide_generator rules must have names that end in .generator (${NAME})")
  endif()

  string(REGEX REPLACE "\\.generator*$" "" BASENAME ${NAME})

  if ("${args_GENERATOR_NAME}" STREQUAL "")
    set(args_GENERATOR_NAME "${BASENAME}")
  endif()

  # We could precompile GenGen.cpp, but add_executable() requires
  # at least one source file, and this is the cheapest one we're going to have.
  add_executable("${NAME}_binary" "${HALIDE_TOOLS_DIR}/GenGen.cpp")
  _halide_set_cxx_options("${NAME}_binary")
  target_include_directories("${NAME}_binary" PRIVATE "${HALIDE_TOOLS_DIR}")
  target_link_libraries("${NAME}_binary" PRIVATE ${HALIDE_COMPILER_LIB} ${HALIDE_SYSTEM_LIBS} ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
  if (MSVC)
    target_link_libraries("${NAME}_binary" PRIVATE Kernel32)
  endif()

  list(LENGTH args_SRCS SRCSLEN)
  # Don't create an empty object-library: that can cause quiet failures in MSVC builds.
  if("${SRCSLEN}" GREATER 0)
    set(GENLIB "${NAME}_library")
    # Use Shared Libraries for all Generators to ensure that the RegisterGenerator
    # code is not dead-stripped.
    add_library("${GENLIB}" STATIC ${args_SRCS})
    _halide_set_cxx_options("${GENLIB}")
    target_link_libraries("${GENLIB}" ${args_DEPS})
    target_include_directories("${GENLIB}" PRIVATE ${args_INCLUDES} "${HALIDE_INCLUDE_DIR}" "${HALIDE_TOOLS_DIR}")
    foreach(DEP ${args_DEPS})
      target_include_directories("${GENLIB}" PRIVATE 
                                 $<TARGET_PROPERTY:${DEP},INTERFACE_INCLUDE_DIRECTORIES>)
    endforeach()
    # Ensure that Halide.h is built prior to any Generator
    add_dependencies("${GENLIB}" ${HALIDE_COMPILER_LIB})

    # We need to ensure that the libraries are linked in with --whole-archive
    # (or the equivalent), to ensure that the Generator-registration code
    # isn't omitted. Sadly, there's no portable way to do this, so we do some
    # special-casing here:
    if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
      target_link_libraries("${NAME}_binary" PRIVATE "${GENLIB}")
      get_property(GENLIB_DIR TARGET ${GENLIB} PROPERTY LIBRARY_OUTPUT_DIRECTORY)
      if (NOT "${GENLIB_DIR}" STREQUAL "")
        set(GENLIB_DIR "${GENLIB_DIR}/")
      endif()
      set_target_properties("${NAME}_binary" PROPERTIES LINK_FLAGS -Wl,-force_load,${GENLIB_DIR}lib${GENLIB}.a)
    elseif(MSVC)
      # Note that this requires VS2015 R2+
      target_link_libraries("${NAME}_binary" PRIVATE "${GENLIB}")
      set_target_properties("${NAME}_binary" PROPERTIES LINK_FLAGS "/WHOLEARCHIVE:${GENLIB}.lib")
    else()
      # Assume Linux or similar
      target_link_libraries("${NAME}_binary" PRIVATE -Wl,--whole-archive "${GENLIB}" -Wl,-no-whole-archive)
    endif()
  endif()

  _halide_genfiles_dir(${BASENAME} GENFILES_DIR)
  set(STUB_HDR "${GENFILES_DIR}/${BASENAME}.stub.h")
  set(GENERATOR_EXEC_ARGS "-g" "${args_GENERATOR_NAME}" "-o" "${GENFILES_DIR}" "-e" "cpp_stub" "-n" "${BASENAME}")

  _halide_add_exec_generator_target(
    "${NAME}_stub_gen"
    GENERATOR_BINARY "${NAME}_binary"
    GENERATOR_ARGS   "${GENERATOR_EXEC_ARGS}"
    OUTPUTS          "${STUB_HDR}"
  )
  set_property(TARGET "${NAME}_stub_gen" PROPERTY _HALIDE_GENERATOR_NAME "${args_GENERATOR_NAME}")

  # Make a header-only library that exports the include path
  add_library("${NAME}" INTERFACE)
  add_dependencies("${NAME}" "${NAME}_stub_gen")
  set_target_properties("${NAME}" PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${GENFILES_DIR}")
  if("${SRCSLEN}" GREATER 0)
    set_target_properties("${NAME}" PROPERTIES INTERFACE_LINK_LIBRARIES "${GENLIB}")
  endif()
endfunction()

# Use a Generator target to emit a code library.
function(halide_library_from_generator BASENAME)
  set(options )
  set(oneValueArgs FUNCTION_NAME GENERATOR HALIDE_TARGET)
  set(multiValueArgs EXTRA_OUTPUTS FILTER_DEPS GENERATOR_ARGS HALIDE_TARGET_FEATURES INCLUDES)
  cmake_parse_arguments(args "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if ("${args_GENERATOR}" STREQUAL "")
    set(args_GENERATOR "${BASENAME}.generator")
  endif()
  if ("${args_FUNCTION_NAME}" STREQUAL "")
    set(args_FUNCTION_NAME "${BASENAME}")
  endif()
  if ("${args_HALIDE_TARGET}" STREQUAL "")
    set(args_HALIDE_TARGET "host")
  endif()
  # It's fine for EXTRA_OUTPUTS, GENERATOR_ARGS, FILTER_DEPS, HALIDE_TARGET_FEATURES to be empty

  # Some sanity checking
  if("${args_HALIDE_TARGET}" MATCHES "^target=")
    message(FATAL_ERROR "HALIDE_TARGET should not begin with 'target='.")
  endif()
  foreach(FEATURE ${args_HALIDE_TARGET_FEATURES})
    if("${FEATURE}" STREQUAL "no_runtime")
      message(FATAL_ERROR "HALIDE_TARGET_FEATURES may not contain 'no_runtime'.")
    endif()
    # Note that this list isn't exhaustive, but will check enough of the likely
    # common cases to enforce proper usage.
    if("${FEATURE}" STREQUAL "host" OR
       "${FEATURE}" STREQUAL "x86" OR
       "${FEATURE}" STREQUAL "arm" OR
       "${FEATURE}" STREQUAL "32" OR
       "${FEATURE}" STREQUAL "64" OR
       "${FEATURE}" STREQUAL "linux" OR
       "${FEATURE}" STREQUAL "osx" OR
       "${FEATURE}" STREQUAL "windows" OR
       "${FEATURE}" STREQUAL "ios" OR
       "${FEATURE}" STREQUAL "android")
      message(FATAL_ERROR "HALIDE_TARGET_FEATURES may not the Arch/OS/Bits string '${FEATURE}'; use HALIDE_TARGET instead.")
    endif()
  endforeach()
  foreach(ARG ${args_GENERATOR_ARGS})
    if("${ARG}" MATCHES "^target=")
      message(FATAL_ERROR "GENERATOR_ARGS may not include 'target=whatever'; use HALIDE_TARGET instead.")
    endif()
  endforeach()

  set(OUTPUTS static_library h)
  foreach(E ${args_EXTRA_OUTPUTS})
    if("${E}" STREQUAL "cpp")
      message(FATAL_ERROR "halide_library('${BASENAME}') doesn't support 'cpp' in EXTRA_OUTPUTS; please depend on '${BASENAME}_cc' instead.")
    endif()
    if("${E}" STREQUAL "cpp_stub")
      message(FATAL_ERROR "halide_library('${BASENAME}') doesn't support 'cpp_stub' in EXTRA_OUTPUTS; please depend on '${BASENAME}.generator' instead.")
    endif()
    list(FIND OUTPUTS ${E} index)
    if (${index} GREATER -1)
      message(FATAL_ERROR "Duplicate entry ${E} in extra_outputs.")
    endif()
    list(APPEND OUTPUTS ${E})
  endforeach()

  get_property(GENERATOR_NAME TARGET "${args_GENERATOR}_stub_gen" PROPERTY _HALIDE_GENERATOR_NAME)

  # Create a directory to contain generator specific intermediate files
  _halide_genfiles_dir(${BASENAME} GENFILES_DIR)

  # Append HALIDE_TARGET_FEATURES to the target(s)
  set(TARGET_WITH_FEATURES "${args_HALIDE_TARGET}")
  foreach(FEATURE ${args_HALIDE_TARGET_FEATURES})
    _halide_add_target_features("${TARGET_WITH_FEATURES}" ${FEATURE} TARGET_WITH_FEATURES)
  endforeach()
  # Select the runtime to use *before* adding no_runtime
  _halide_library_runtime("${TARGET_WITH_FEATURES}" RUNTIME_NAME)
  _halide_add_target_features("${TARGET_WITH_FEATURES}" "no_runtime" TARGET_WITH_FEATURES)

  set(GENERATOR_EXEC_ARGS "-o" "${GENFILES_DIR}")
  list(APPEND GENERATOR_EXEC_ARGS "-g" "${GENERATOR_NAME}")
  list(APPEND GENERATOR_EXEC_ARGS "-f" "${args_FUNCTION_NAME}" )
  list(APPEND GENERATOR_EXEC_ARGS "-x" ".s=.s.txt,.cpp=.generated.cpp")
  list(APPEND GENERATOR_EXEC_ARGS "target=${TARGET_WITH_FEATURES}")
  # GENERATOR_ARGS always come last
  list(APPEND GENERATOR_EXEC_ARGS ${args_GENERATOR_ARGS})

  # CMake has no map type, and no switch statement. Whee!
  set(OUTPUT_FILES )
  foreach(OUTPUT ${OUTPUTS})
    if ("${OUTPUT}" STREQUAL "static_library")
      list(APPEND OUTPUT_FILES "${GENFILES_DIR}/${BASENAME}${CMAKE_STATIC_LIBRARY_SUFFIX}")
    elseif ("${OUTPUT}" STREQUAL "o")
      if(MSVC)
        list(APPEND OUTPUT_FILES "${GENFILES_DIR}/${BASENAME}.obj")
      else()
        list(APPEND OUTPUT_FILES "${GENFILES_DIR}/${BASENAME}.o")
      endif()
    elseif ("${OUTPUT}" STREQUAL "h")
      list(APPEND OUTPUT_FILES "${GENFILES_DIR}/${BASENAME}.h")
    elseif ("${OUTPUT}" STREQUAL "assembly")
      list(APPEND OUTPUT_FILES "${GENFILES_DIR}/${BASENAME}.s.txt")
    elseif ("${OUTPUT}" STREQUAL "bitcode")
      list(APPEND OUTPUT_FILES "${GENFILES_DIR}/${BASENAME}.bc")
    elseif ("${OUTPUT}" STREQUAL "stmt")
      list(APPEND OUTPUT_FILES "${GENFILES_DIR}/${BASENAME}.stmt")
    elseif ("${OUTPUT}" STREQUAL "html")
      list(APPEND OUTPUT_FILES "${GENFILES_DIR}/${BASENAME}.html")
    # elseif ("${OUTPUT}" STREQUAL "cpp")
    #   list(APPEND OUTPUT_FILES "${GENFILES_DIR}/${BASENAME}.generated.cpp")
    endif()
  endforeach()

  # Output everything (except for the generated .cpp file)
  string(REPLACE ";" "," OUTPUTS_COMMA "${OUTPUTS}")
  set(ARGS_WITH_OUTPUTS "-e" ${OUTPUTS_COMMA} ${GENERATOR_EXEC_ARGS})
  _halide_add_exec_generator_target(
    "${BASENAME}_lib_gen"
    GENERATOR_BINARY "${args_GENERATOR}_binary"
    GENERATOR_ARGS   "${ARGS_WITH_OUTPUTS}"
    OUTPUTS          ${OUTPUT_FILES}
  )

  add_library("${BASENAME}" INTERFACE)
  add_dependencies("${BASENAME}" "${BASENAME}_lib_gen" "${RUNTIME_NAME}")
  set_target_properties("${BASENAME}" PROPERTIES 
      INTERFACE_INCLUDE_DIRECTORIES ${GENFILES_DIR} ${args_INCLUDES}
      INTERFACE_LINK_LIBRARIES "${GENFILES_DIR}/${BASENAME}${CMAKE_STATIC_LIBRARY_SUFFIX};${RUNTIME_NAME};${args_FILTER_DEPS};${CMAKE_DL_LIBS};${CMAKE_THREAD_LIBS_INIT}")

  # A separate invocation for the generated .cpp file, 
  # since it's rarely used, and some code will fail at Generation
  # time at present (e.g. code with predicated loads or stores).
  set(ARGS_WITH_OUTPUTS "-e" "cpp" ${GENERATOR_EXEC_ARGS})
  _halide_add_exec_generator_target(
    "${BASENAME}_cc_gen"
    GENERATOR_BINARY "${args_GENERATOR}_binary"
    GENERATOR_ARGS   "${ARGS_WITH_OUTPUTS}"
    OUTPUTS          "${GENFILES_DIR}/${BASENAME}.generated.cpp"
  )

  add_library("${BASENAME}_cc_lib" STATIC "${GENFILES_DIR}/${BASENAME}.generated.cpp")
  target_include_directories("${BASENAME}_cc_lib" PRIVATE "${HALIDE_INCLUDE_DIR}")
  # Needs _lib_gen as well, to get the .h file
  add_dependencies("${BASENAME}_cc_lib" "${BASENAME}_lib_gen" "${BASENAME}_cc_gen")
  # Very few of the cc_libs are needed, so exclude from "all".
  set_target_properties("${BASENAME}_cc_lib" PROPERTIES EXCLUDE_FROM_ALL TRUE)

  add_library("${BASENAME}_cc" INTERFACE)
  add_dependencies("${BASENAME}_cc" "${BASENAME}_cc_lib")
  set_target_properties("${BASENAME}_cc" PROPERTIES 
      INTERFACE_INCLUDE_DIRECTORIES ${GENFILES_DIR} ${args_INCLUDES}
      INTERFACE_LINK_LIBRARIES "${BASENAME}_cc_lib;${args_FILTER_DEPS}")

  # Code to build the BASENAME.rungen target
  set(RUNGEN "${BASENAME}.rungen")
  add_executable("${RUNGEN}" "${HALIDE_TOOLS_DIR}/RunGenStubs.cpp")
  target_compile_definitions("${RUNGEN}" PRIVATE "-DHL_RUNGEN_FILTER_HEADER=\"${BASENAME}.h\"")
  target_link_libraries("${RUNGEN}" PRIVATE _halide_library_from_generator_rungen "${BASENAME}")
  # Not all Generators will build properly with RunGen (e.g., missing
  # external dependencies), so exclude them from the "ALL" targets
  set_target_properties("${RUNGEN}" PROPERTIES EXCLUDE_FROM_ALL TRUE)

  # BASENAME.run simply runs the BASENAME.rungen target
  add_custom_target("${BASENAME}.run" 
                    COMMAND "${RUNGEN}" "$(RUNARGS)"
                    DEPENDS "${RUNGEN}")
  set_target_properties("${BASENAME}.run" PROPERTIES EXCLUDE_FROM_ALL TRUE)
endfunction()

# Rule to build and use a Generator; it's convenient sugar around 
# halide_generator() + halide_library_from_generator().
function(halide_library NAME)
  set(oneValueArgs FUNCTION_NAME HALIDE_TARGET GENERATOR GENERATOR_NAME)
  set(multiValueArgs EXTRA_OUTPUTS FILTER_DEPS GENERATOR_DEPS HALIDE_TARGET_FEATURES INCLUDES GENERATOR_ARGS SRCS)
  cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if (NOT "${args_GENERATOR}" STREQUAL "")
    message(FATAL_ERROR "halide_library('${BASENAME}') doesn't take a GENERATOR argument. Did you mean to use GENERATOR_NAME, or the halide_library_from_generator() rule?")
  endif()

  halide_generator("${NAME}.generator"
                   SRCS ${args_SRCS}
                   DEPS ${args_GENERATOR_DEPS}
                   INCLUDES ${args_INCLUDES}
                   GENERATOR_NAME ${args_GENERATOR_NAME})

  halide_library_from_generator("${NAME}"
                   DEPS ${args_FILTER_DEPS}
                   INCLUDES ${args_INCLUDES}
                   GENERATOR "${NAME}.generator"
                   FUNCTION_NAME ${args_FUNCTION_NAME}
                   HALIDE_TARGET ${args_HALIDE_TARGET}
                   HALIDE_TARGET_FEATURES ${args_HALIDE_TARGET_FEATURES}
                   GENERATOR_ARGS ${args_GENERATOR_ARGS}
                   EXTRA_OUTPUTS ${args_EXTRA_OUTPUTS})
endfunction()

# ----------------------- Private Functions. 
# All functions, properties, variables, etc. that being with an underscore
# should be assumed to be private implementation details; don't rely on them externally.

# Set the C++ options necessary for using libHalide.
function(_halide_set_cxx_options TARGET)
  set_target_properties("${TARGET}" PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
  target_compile_definitions("${TARGET}" PRIVATE "-DHalide_${HALIDE_LIBRARY_TYPE}")
  if (MSVC)
    target_compile_definitions("${TARGET}" PUBLIC "-D_CRT_SECURE_NO_WARNINGS" "-D_SCL_SECURE_NO_WARNINGS")
    target_compile_options("${TARGET}" PRIVATE "/GR-")
  else()
    target_compile_options("${TARGET}" PRIVATE "-fno-rtti")
  endif()
endfunction()

# Get (and lazily create) the generated-files directory for Generators.
function(_halide_genfiles_dir NAME OUTVAR)
  set(GENFILES_DIR "${CMAKE_BINARY_DIR}/genfiles/${NAME}")
  file(MAKE_DIRECTORY "${GENFILES_DIR}")
  set(${OUTVAR} "${GENFILES_DIR}" PARENT_SCOPE)
endfunction()

# Adds features to a target string, canonicalizing the result.
# If multitarget, features are added to all.
function(_halide_add_target_features HALIDE_TARGET HALIDE_FEATURES OUTVAR)
  string(REPLACE "," ";" MULTITARGETS "${HALIDE_TARGET}")
  foreach(T ${MULTITARGETS})
    string(REPLACE "-" ";" NEW_T "${T}")
    foreach(F ${HALIDE_FEATURES})
      list(APPEND NEW_T ${F})
    endforeach()
    string(REPLACE ";" "-" NEW_T "${NEW_T}")
    _halide_canonicalize_target("${NEW_T}" NEW_T)    
    list(APPEND NEW_MULTITARGETS ${NEW_T})
  endforeach()
  string(REPLACE ";" "," NEW_MULTITARGETS "${NEW_MULTITARGETS}")
  set(${OUTVAR} "${NEW_MULTITARGETS}" PARENT_SCOPE)
endfunction()

# Split the target into base and feature lists.
function(_halide_split_target HALIDE_TARGET OUTVAR_BASE OUTVAR_FEATURES)
  if("${HALIDE_TARGET}" MATCHES ".*,.*")
    message(FATAL_ERROR "Multitarget may not be specified in _halide_split_target(${HALIDE_TARGET})")
  endif()

  string(REPLACE "-" ";" FEATURES "${HALIDE_TARGET}")
  list(LENGTH FEATURES LEN)
  if("${LEN}" EQUAL 0)
    message(FATAL_ERROR "Empty target")
  endif()

  list(GET FEATURES 0 BASE)
  if ("${BASE}" STREQUAL "host")
    list(REMOVE_AT FEATURES 0)
  else()
    if("${LEN}" LESS 3)
      message(FATAL_ERROR "Illegal target (${HALIDE_TARGET})")
    endif()
    list(GET FEATURES 0 1 2 BASE)
    list(REMOVE_AT FEATURES 0 1 2)
  endif()
  set(${OUTVAR_BASE} "${BASE}" PARENT_SCOPE)
  set(${OUTVAR_FEATURES} "${FEATURES}" PARENT_SCOPE)
endfunction()

# Join base and feature lists back into a target. Do not canonicalize.
function(_halide_join_target BASE FEATURES OUTVAR)
  foreach(F ${FEATURES})
    list(APPEND BASE ${F})
  endforeach()
  string(REPLACE ";" "-" BASE "${BASE}")
  set(${OUTVAR} "${BASE}" PARENT_SCOPE)
endfunction()

# Alphabetizes the features part of the target to make sure they always match no
# matter the concatenation order of the target string pieces. Remove duplicates.
function(_halide_canonicalize_target HALIDE_TARGET OUTVAR)
  if("${HALIDE_TARGET}" MATCHES ".*,.*")
    message(FATAL_ERROR "Multitarget may not be specified in _halide_canonicalize_target(${HALIDE_TARGET})")
  endif()
  _halide_split_target("${HALIDE_TARGET}" BASE FEATURES)
  list(REMOVE_DUPLICATES FEATURES)
  list(SORT FEATURES)
  _halide_join_target("${BASE}" "${FEATURES}" HALIDE_TARGET)
  set(${OUTVAR} "${HALIDE_TARGET}" PARENT_SCOPE)
endfunction()

# Given a HALIDE_TARGET, return the CMake target name for the runtime.
function(_halide_runtime_target_name HALIDE_TARGET OUTVAR)
  # MULTITARGETS = HALIDE_TARGET.split(",")
  string(REPLACE "," ";" MULTITARGETS "${HALIDE_TARGET}")
  # HALIDE_TARGET = MULTITARGETS.final_element()
  list(GET MULTITARGETS -1 HALIDE_TARGET)
  _halide_canonicalize_target("${HALIDE_TARGET}" HALIDE_TARGET)
  _halide_split_target("${HALIDE_TARGET}" BASE FEATURES)
  # Discard target features which do not affect the contents of the runtime.
  list(REMOVE_DUPLICATES FEATURES)
  list(REMOVE_ITEM FEATURES "user_context" "no_asserts" "no_bounds_query" "profile")
  list(SORT FEATURES)
  # Now build up the name
  set(RESULT "halide_library_runtime")
  foreach(B ${BASE})
    list(APPEND RESULT ${B})
  endforeach()
  foreach(F ${FEATURES})
    list(APPEND RESULT ${F})
  endforeach()
  string(REPLACE ";" "_" RESULT "${RESULT}")
  set(${OUTVAR} "${RESULT}" PARENT_SCOPE)
endfunction()

# Generate the runtime library for the given halide_target; return
# its cmake target name in outvar.
function(_halide_library_runtime HALIDE_TARGET OUTVAR)
  if(NOT TARGET halide_library_runtime.generator)
    halide_generator(halide_library_runtime.generator SRCS "")
  endif()

  string(REPLACE "," ";" MULTITARGETS "${HALIDE_TARGET}")
  list(GET MULTITARGETS -1 HALIDE_TARGET)
  _halide_runtime_target_name("${HALIDE_TARGET}" RUNTIME_NAME)
  if(NOT TARGET "${RUNTIME_NAME}")
    set(RUNTIME_LIB "${RUNTIME_NAME}${CMAKE_STATIC_LIBRARY_SUFFIX}")

    _halide_genfiles_dir(${RUNTIME_NAME} GENFILES_DIR)
    set(GENERATOR_EXEC_ARGS "-o" "${GENFILES_DIR}")
    list(APPEND GENERATOR_EXEC_ARGS "-r" "${RUNTIME_NAME}")
    list(APPEND GENERATOR_EXEC_ARGS "target=${HALIDE_TARGET}")

    _halide_add_exec_generator_target(
      "${RUNTIME_NAME}_runtime_gen"
      GENERATOR_BINARY "halide_library_runtime.generator_binary"
      GENERATOR_ARGS   "${GENERATOR_EXEC_ARGS}"
      OUTPUTS          "${GENFILES_DIR}/${RUNTIME_LIB}"
    )

    add_library("${RUNTIME_NAME}" INTERFACE)
    add_dependencies("${RUNTIME_NAME}" "${RUNTIME_NAME}_runtime_gen")
    set_target_properties("${RUNTIME_NAME}" PROPERTIES INTERFACE_LINK_LIBRARIES "${GENFILES_DIR}/${RUNTIME_LIB}")
  endif()
  set(${OUTVAR} "${RUNTIME_NAME}" PARENT_SCOPE)  
endfunction()

function(_halide_add_exec_generator_target EXEC_TARGET)
  set(options )
  set(oneValueArgs GENERATOR_BINARY)
  set(multiValueArgs OUTPUTS GENERATOR_ARGS)
  cmake_parse_arguments(args "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  add_custom_target(${EXEC_TARGET} DEPENDS ${args_OUTPUTS})

  # As of CMake 3.x, add_custom_command() recognizes executable target names in its COMMAND.
  add_custom_command(
    OUTPUT ${args_OUTPUTS}
    DEPENDS ${args_GENERATOR_BINARY}
    COMMAND ${args_GENERATOR_BINARY} ${args_GENERATOR_ARGS}
  )
  foreach(OUT ${args_OUTPUTS})
    set_source_files_properties(${OUT} PROPERTIES GENERATED TRUE)
  endforeach()
endfunction()

# ----------------------- Configuration code

# If paths to tools, include, and libHalide aren't specified, infer them
# based on the path to the distrib folder. If the path to the distrib
# folder isn't specified, fail.
if("${HALIDE_TOOLS_DIR}" STREQUAL "" OR 
    "${HALIDE_INCLUDE_DIR}" STREQUAL "" OR 
    "${HALIDE_COMPILER_LIB}" STREQUAL "")
  if("${HALIDE_DISTRIB_DIR}" STREQUAL "")
    message(FATAL_ERROR "HALIDE_DISTRIB_DIR must point to the Halide distribution directory.")
  endif()
  set(HALIDE_INCLUDE_DIR "${HALIDE_DISTRIB_DIR}/include")
  set(HALIDE_TOOLS_DIR "${HALIDE_DISTRIB_DIR}/tools")
  add_library(_halide_compiler_lib INTERFACE)
  set_target_properties(_halide_compiler_lib PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${HALIDE_INCLUDE_DIR})
  set_target_properties(_halide_compiler_lib PROPERTIES INTERFACE_LINK_LIBRARIES "${HALIDE_DISTRIB_DIR}/lib/libHalide${CMAKE_STATIC_LIBRARY_SUFFIX}")
  set(HALIDE_COMPILER_LIB _halide_compiler_lib)
endif()

if("${HALIDE_SYSTEM_LIBS}" STREQUAL "")
  # If HALIDE_SYSTEM_LIBS isn't defined, we are compiling against a Halide distribution
  # folder; this is normally captured in the halide_config.cmake file. If that file
  # exists in the same directory as this one, just include it here.
  if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/halide_config.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/halide_config.cmake")
  else()
    message(FATAL_ERROR "HALIDE_SYSTEM_LIBS is not set and we could not find halide_config.cmake")
  endif()
endif()

define_property(TARGET PROPERTY _HALIDE_GENERATOR_NAME
                BRIEF_DOCS "Internal use by Halide build rules: do not use externally"
                FULL_DOCS "Internal use by Halide build rules: do not use externally")

add_library(_halide_library_from_generator_rungen "${HALIDE_TOOLS_DIR}/RunGen.cpp")
target_include_directories(_halide_library_from_generator_rungen PRIVATE "${HALIDE_INCLUDE_DIR}")
halide_use_image_io(_halide_library_from_generator_rungen)
_halide_set_cxx_options(_halide_library_from_generator_rungen)
set_target_properties(_halide_library_from_generator_rungen PROPERTIES EXCLUDE_FROM_ALL TRUE)
