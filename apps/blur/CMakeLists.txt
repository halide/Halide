# Generator
halide_library(halide_blur SRCS halide_blur_generator.cpp)

# Final executable
add_executable(blur_test test.cpp)
target_link_libraries(blur_test PUBLIC halide_blur)

if (NOT MSVC)
  target_compile_options(blur_test PRIVATE "-O2")
  if (OPENMP_FOUND)
    target_compile_options(blur_test PRIVATE ${OpenMP_CXX_FLAGS})
    target_link_libraries(blur_test PRIVATE ${OpenMP_CXX_FLAGS})
  else()
    target_compile_options(blur_test PRIVATE "-Wno-unknown-pragmas")
  endif()
endif()
