# Generator
halide_project(pipeline "apps" pipeline.cpp)
halide_project(pipeline_cpp "apps" pipeline_cpp.cpp)

set(pipeline_c_h "${CMAKE_CURRENT_BINARY_DIR}/pipeline_c.h")
set(pipeline_c_src "${CMAKE_CURRENT_BINARY_DIR}/pipeline_c.cpp")
set(pipeline_native_h "${CMAKE_CURRENT_BINARY_DIR}/pipeline_native.h")
set(pipeline_native_obj "${CMAKE_CURRENT_BINARY_DIR}/pipeline_native.o")

# Final executable
set(run_target run_c_backend_and_native)
add_executable(${run_target} run.cpp ${pipeline_c_src} ${pipeline_c_h} ${pipeline_native_h})
target_compile_options(${run_target} PUBLIC "-std=c++11")
target_link_libraries(${run_target} PRIVATE ${pipeline_native_obj})
target_include_directories(${run_target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
if (NOT WIN32)
  target_link_libraries(${run_target} PRIVATE dl pthread)
endif()

# HACK: Emitted C code isn't valid C code, just compile with
# C++ compiler for now
set_property(SOURCE "${pipeline_c_src}" PROPERTY LANGUAGE CXX)

# FIXME: Cannot use halide_add_generator_dependency() because
# pipeline.cpp doesn't handle the commandline args passed.
add_custom_command(OUTPUT "${pipeline_c_h}" "${pipeline_c_src}"
                          "${pipeline_native_h}" "${pipeline_native_obj}"
                   COMMAND pipeline
                   WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                   COMMENT "Generating pipeline outputs"
                  )

set(pipeline_cpp_cpp_h "${CMAKE_CURRENT_BINARY_DIR}/pipeline_cpp_cpp.h")
set(pipeline_cpp_cpp_src "${CMAKE_CURRENT_BINARY_DIR}/pipeline_cpp_cpp.cpp")
set(pipeline_cpp_native_h "${CMAKE_CURRENT_BINARY_DIR}/pipeline_cpp_native.h")
set(pipeline_cpp_native_obj "${CMAKE_CURRENT_BINARY_DIR}/pipeline_cpp_native.o")

# Final executable for C++ side
set(run_target_cpp run_c_backend_and_native_cpp)
add_executable(${run_target_cpp} run_cpp.cpp ${pipeline_cpp_cpp_src} ${pipeline_cpp_cpp_h} ${pipeline_cpp_native_h})
target_compile_options(${run_target_cpp} PUBLIC "-std=c++11")
target_link_libraries(${run_target_cpp} PRIVATE ${pipeline_cpp_native_obj})
target_include_directories(${run_target_cpp} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
if (NOT WIN32)
  target_link_libraries(${run_target_cpp} PRIVATE dl pthread)
endif()

set_property(SOURCE "${pipeline_cpp_cpp_src}" PROPERTY LANGUAGE CXX)

# FIXME: Cannot use halide_add_generator_dependency() because
# pipeline.cpp doesn't handle the commandline args passed.
add_custom_command(OUTPUT "${pipeline_cpp_cpp_h}" "${pipeline_cpp_cpp_src}"
                          "${pipeline_cpp_native_h}" "${pipeline_cpp_native_obj}"
                   COMMAND pipeline_cpp
                   WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                   COMMENT "Generating pipeline outputs"
                  )
