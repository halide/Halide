# Generator
halide_project(pipeline "apps" pipeline.cpp)

set(pipeline_c_h "${CMAKE_CURRENT_BINARY_DIR}/pipeline_c.h")
set(pipeline_c_src "${CMAKE_CURRENT_BINARY_DIR}/pipeline_c.c")
set(pipeline_native_h "${CMAKE_CURRENT_BINARY_DIR}/pipeline_native.h")
set(pipeline_native_obj "${CMAKE_CURRENT_BINARY_DIR}/pipeline_native.o")

# Final executable
set(run_target run_c_backend_and_native)
add_executable(${run_target} run.cpp ${pipeline_c_src} ${pipeline_c_h} ${pipeline_native_h})
target_link_libraries(${run_target} PRIVATE ${pipeline_native_obj})
target_include_directories(${run_target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
if (NOT WIN32)
  target_link_libraries(${run_target} PRIVATE dl pthread)
endif()
set_target_properties(${run_target} pipeline PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                      "${CMAKE_CURRENT_BINARY_DIR}")

# HACK: Emitted C code isn't valid C code, just compile with
# C++ compiler for now
set_property(SOURCE "${pipeline_c_src}" PROPERTY LANGUAGE CXX)

# FIXME: Cannot use halide_add_generator_dependency() because
# pipeline.cpp doesn't handle the commandline args passed.
add_custom_command(OUTPUT "${pipeline_c_h}" "${pipeline_c_src}"
                          "${pipeline_native_h}" "${pipeline_native_obj}"
                   COMMAND pipeline
                   COMMENT "Generating pipeline outputs"
                  )
