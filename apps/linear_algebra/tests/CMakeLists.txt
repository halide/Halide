if (NOT CBLAS_FOUND)
  message(STATUS "linear_algebra: No CBLAS header, skipping CBLAS tests")
  return()
endif()

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
target_compile_options(test_halide_blas PRIVATE -Wno-unused-variable)

target_link_libraries(test_halide_blas
  PRIVATE
   halide_blas
   cblas # XXX fragile
   ${HALIDE_COMPILER_LIB}
)

