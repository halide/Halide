# Generator
halide_project(halide_blur "apps" halide_blur.cpp)
set(halide_blur_h "${CMAKE_CURRENT_BINARY_DIR}/halide_blur.h")
set(halide_blur_lib "${CMAKE_CURRENT_BINARY_DIR}/halide_blur${CMAKE_STATIC_LIBRARY_SUFFIX}")

# Final executable
add_executable(blur_test test.cpp ${halide_blur_h})
target_link_libraries(blur_test PRIVATE "${halide_blur_lib}")
target_include_directories(blur_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
if (NOT WIN32)
  target_link_libraries(blur_test PRIVATE dl pthread)
endif()

if (NOT MSVC)
  target_compile_options(blur_test PRIVATE "-std=c++11")
  check_cxx_compiler_flag("-Wno-cast-qual" SUPPORTS_NO_CAST_QUAL)
  if (SUPPORTS_NO_CAST_QUAL)
    target_compile_options(blur_test PRIVATE "-Wno-cast-qual")
  endif()
  target_compile_options(blur_test PRIVATE "-std=c++11" "-msse2")
  if (OPENMP_FOUND)
    target_compile_options(blur_test PRIVATE ${OpenMP_CXX_FLAGS})
    target_link_libraries(blur_test PRIVATE ${OpenMP_CXX_FLAGS})
  else()
    target_compile_options(blur_test PRIVATE "-Wno-unknown-pragmas")
  endif()
endif()


# FIXME: Cannot use halide_add_generator_dependency() because
# halide_blur doesn't handle the commandline args passed.
add_custom_command(OUTPUT "${halide_blur_h}" "${halide_blur_lib}"
                   COMMAND halide_blur
                   WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                   COMMENT "Generating halide_blur"
                  )
