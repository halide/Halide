# This function adds custom build steps to invoke a Halide generator exectuable,
# produce a static library containing the generated code, and then link that
# static library to the specified target. The generator executable must be
# produced separately, e.g. using a call to the function halide_project(...)
function(halide_add_generator_dependency target gen_target gen_name func_name)

  # Determine a scratch directory to build and execute the generator. ${target}
  # will include the generated header from this directory.
  file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/" NATIVE_INT_DIR)
  set(SCRATCH_DIR "${NATIVE_INT_DIR}scratch_${gen_name}")
  file(MAKE_DIRECTORY "${SCRATCH_DIR}")

  # CMake 2.8 doesn't have string(CONCAT), so fake it like so:
  string(REPLACE ".lib" "${CMAKE_STATIC_LIBRARY_SUFFIX}" FILTER_LIB "${func_name}.lib" )
  string(REPLACE ".h" ".h" FILTER_HDR "${func_name}.h" )

  # Add a custom target to output the Halide generated static library
  if (WIN32)
    # TODO(srj): this has not yet been tested on Windows.
    add_custom_command(OUTPUT "${FILTER_LIB}" "${FILTER_HDR}"
      DEPENDS "${gen_target}"
      COMMAND "${CMAKE_BINARY_DIR}/bin/${BUILD_TYPE}/${gen_target}${CMAKE_EXECUTABLE_SUFFIX}" "-g" "${gen_name}" "-f" "${func_name}" "-o" "${SCRATCH_DIR}" ${ARGN}
      COMMAND "lib.exe" "/OUT:${FILTER_LIB}" "${SCRATCH_DIR}\\${func_name}.o"
      WORKING_DIRECTORY "${SCRATCH_DIR}"
      )
  elseif(XCODE)
    # The generator executable will be placed in a configuration specific
    # directory, so the Xcode variable $(CONFIGURATION) is passed in the custom
    # build script. Also, the build uses libtool to create the static library.
    add_custom_command(OUTPUT "${FILTER_LIB}" "${FILTER_HDR}"
      DEPENDS "${gen_target}"
      COMMAND "${CMAKE_BINARY_DIR}/bin/$(CONFIGURATION)/${gen_target}${CMAKE_EXECUTABLE_SUFFIX}" "-g" "${gen_name}" "-f" "${func_name}" "-o" "${SCRATCH_DIR}" ${ARGN}
      COMMAND libtool -static -o "${FILTER_LIB}" "${SCRATCH_DIR}/${func_name}.o"
      WORKING_DIRECTORY "${SCRATCH_DIR}"
      )
  else()
    add_custom_command(OUTPUT "${FILTER_LIB}" "${FILTER_HDR}"
      DEPENDS "${gen_target}"
      COMMAND "${CMAKE_BINARY_DIR}/bin/${gen_target}${CMAKE_EXECUTABLE_SUFFIX}" "-g" "${gen_name}" "-f" "${func_name}" "-o" "${SCRATCH_DIR}" ${ARGN}
      COMMAND "${CMAKE_AR}" q "${FILTER_LIB}" "${SCRATCH_DIR}/${func_name}.o"
      WORKING_DIRECTORY "${SCRATCH_DIR}"
      )
  endif()

  # Use a custom target to force it to run the generator before the
  # object file for the runner. The target name will start with the prefix
  #  "exec_generator_"
  add_custom_target("exec_generator_${gen_name}_${func_name}"
                    DEPENDS "${FILTER_LIB}" "${FILTER_HDR}"
                    )

  # Place the target in a special folder in IDEs
  set_target_properties("exec_generator_${gen_name}_${func_name}" PROPERTIES
                        FOLDER "generator"
                        )

  # Make the generator execution target a dependency of the specified
  # application target and link to the generated library

  # exec_generator_foo must build before $target
  add_dependencies("${target}" "exec_generator_${gen_name}_${func_name}")

  target_link_libraries("${target}" "${SCRATCH_DIR}/${FILTER_LIB}")

  # Add the scratch directory to the include path for ${target}. The generated
  # header may be included via #include "${gen_name}.h"
  target_include_directories("${target}" PRIVATE "${SCRATCH_DIR}")

endfunction(halide_add_generator_dependency)
