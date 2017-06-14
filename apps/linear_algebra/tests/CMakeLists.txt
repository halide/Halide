if (NOT CBLAS_FOUND)
  message(WARNING "No CBLAS header, skipping tests")
  return()
endif()

set(MY_CXX_FLAGS -O3 -Wall -std=c++11 -march=native)

add_executable(test_halide_blas
  test_halide_blas.cpp
)
target_include_directories(test_halide_blas SYSTEM
  PRIVATE
   ${CBLAS_INCLUDE_DIR}
)
target_include_directories(test_halide_blas BEFORE
  PRIVATE
    ${halide_blas_INCLUDE_DIRS}
)
target_compile_options(test_halide_blas PRIVATE ${MY_CXX_FLAGS} -Wno-unused-variable)

target_link_libraries(test_halide_blas
  PRIVATE
   halide_blas
   cblas # XXX fragile
   Halide
)
