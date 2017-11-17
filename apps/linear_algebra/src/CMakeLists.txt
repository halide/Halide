add_library(halide_blas halide_blas.cpp)
target_include_directories(halide_blas PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Define all our generators
halide_generator(saxpy.generator SRCS blas_l1_generators.cpp)
halide_generator(daxpy.generator SRCS blas_l1_generators.cpp)
halide_generator(sdot.generator SRCS blas_l1_generators.cpp)
halide_generator(ddot.generator SRCS blas_l1_generators.cpp)
halide_generator(sasum.generator SRCS blas_l1_generators.cpp)
halide_generator(dasum.generator SRCS blas_l1_generators.cpp)

halide_generator(sgemv.generator SRCS blas_l2_generators.cpp)
halide_generator(dgemv.generator SRCS blas_l2_generators.cpp)
halide_generator(sger.generator SRCS blas_l2_generators.cpp)
halide_generator(dger.generator SRCS blas_l2_generators.cpp)

halide_generator(sgemm.generator SRCS blas_l3_generators.cpp)
halide_generator(dgemm.generator SRCS blas_l3_generators.cpp)

# Function to reduce boilerplate
function(add_halide_blas_library)
  set(options )
  set(oneValueArgs TARGET NAME)
  set(multiValueArgs GENERATOR_ARGS)
  cmake_parse_arguments(args "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
  halide_library_from_generator(${args_TARGET}
      GENERATOR ${args_NAME}.generator
      HALIDE_TARGET_FEATURES no_asserts no_bounds_query
      GENERATOR_ARGS ${args_ARGN}
  )
  target_link_libraries(halide_blas PUBLIC ${args_TARGET})
endfunction()

# And now all the instantiaons
add_halide_blas_library(
    TARGET halide_scopy_impl
    NAME saxpy
    GENERATOR_ARGS vectorize=true scale_x=false add_to_y=false)

add_halide_blas_library(
    TARGET halide_dcopy_impl
    NAME daxpy
    GENERATOR_ARGS vectorize=true scale_x=false add_to_y=false)

add_halide_blas_library(
    TARGET halide_sscal_impl
    NAME saxpy
    GENERATOR_ARGS vectorize=true scale_x=true add_to_y=false)

add_halide_blas_library(
    TARGET halide_dscal_impl
    NAME daxpy
    GENERATOR_ARGS vectorize=true scale_x=true add_to_y=false)

add_halide_blas_library(
    TARGET halide_saxpy_impl
    NAME saxpy
    GENERATOR_ARGS vectorize=true scale_x=true add_to_y=true)

add_halide_blas_library(
    TARGET halide_daxpy_impl
    NAME daxpy
    GENERATOR_ARGS vectorize=true scale_x=true add_to_y=true)

add_halide_blas_library(
    TARGET halide_sdot
    NAME sdot
    GENERATOR_ARGS vectorize=true)

add_halide_blas_library(
    TARGET halide_ddot
    NAME ddot
    GENERATOR_ARGS vectorize=true)

add_halide_blas_library(
    TARGET halide_sasum
    NAME sasum
    GENERATOR_ARGS vectorize=true)

add_halide_blas_library(
    TARGET halide_dasum
    NAME dasum
    GENERATOR_ARGS vectorize=true)

add_halide_blas_library(
    TARGET halide_sgemv_notrans
    NAME sgemv
    GENERATOR_ARGS parallel=false vectorize=true transpose=false)

add_halide_blas_library(
    TARGET halide_dgemv_notrans
    NAME dgemv
    GENERATOR_ARGS parallel=false vectorize=true transpose=false)

add_halide_blas_library(
    TARGET halide_sgemv_trans
    NAME sgemv
    GENERATOR_ARGS parallel=false vectorize=true transpose=true)

add_halide_blas_library(
    TARGET halide_dgemv_trans
    NAME dgemv
    GENERATOR_ARGS parallel=false vectorize=true transpose=true)

add_halide_blas_library(
    TARGET halide_sger_impl
    NAME sger
    GENERATOR_ARGS parallel=false vectorize=true)

add_halide_blas_library(
    TARGET halide_dger_impl
    NAME dger
    GENERATOR_ARGS parallel=false vectorize=true)

add_halide_blas_library(
    TARGET halide_sgemm_notrans
    NAME sgemm
    GENERATOR_ARGS transpose_A=false transpose_B=false)

add_halide_blas_library(
    TARGET halide_dgemm_notrans
    NAME dgemm
    GENERATOR_ARGS transpose_A=false transpose_B=false)

add_halide_blas_library(
    TARGET halide_sgemm_transA
    NAME sgemm
    GENERATOR_ARGS transpose_A=true transpose_B=false)

add_halide_blas_library(
    TARGET halide_dgemm_transA
    NAME dgemm
    GENERATOR_ARGS transpose_A=true transpose_B=false)

add_halide_blas_library(
    TARGET halide_sgemm_transB
    NAME sgemm
    GENERATOR_ARGS transpose_A=false transpose_B=true)

add_halide_blas_library(
    TARGET halide_dgemm_transB
    NAME dgemm
    GENERATOR_ARGS transpose_A=false transpose_B=true)

add_halide_blas_library(
    TARGET halide_sgemm_transAB
    NAME sgemm
    GENERATOR_ARGS transpose_A=true transpose_B=true)

add_halide_blas_library(
    TARGET halide_dgemm_transAB
    NAME dgemm
    GENERATOR_ARGS transpose_A=true transpose_B=true)
