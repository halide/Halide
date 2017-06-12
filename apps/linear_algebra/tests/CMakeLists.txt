set(MY_CXX_FLAGS -O3 -Wall -std=c++11 -march=native)

add_executable(test_halide_blas
  test_halide_blas.cpp
)
target_include_directories(test_halide_blas
  PRIVATE
    ../src # XXX
    ${halide_blas_INCLUDE_DIRS}
)
target_compile_options(test_halide_blas PRIVATE ${MY_CXX_FLAGS} -Wno-unused-variable)
target_link_libraries(test_halide_blas
  PRIVATE
   halide_blas
   cblas
   Halide
)
