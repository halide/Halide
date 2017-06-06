set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(CXX_FLAGS -O3 -fopenmp -Wall -std=c++11 -march=native)

halide_add_generator( blas_l1.generator SRCS src/blas_l1_generators.cpp)
halide_add_generator( blas_l2.generator SRCS src/blas_l2_generators.cpp)
halide_add_generator( blas_l3.generator SRCS src/blas_l3_generators.cpp)

add_library(halide_blas
  src/halide_blas.cpp
)
target_compile_options(halide_blas PRIVATE ${CXX_FLAGS})
target_include_directories(halide_blas
  PUBLIC
    src
)

set(GENERATOR_OUTPUTS h static_library)

halide_add_aot_library(halide_scopy_impl
	GENERATOR_TARGET blas_l1.generator
	GENERATOR_NAME saxpy 
	GENERATED_FUNCTION halide_scopy_impl
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host vectorize=true scale_x=false add_to_y=false
)
halide_add_aot_library_dependency(halide_blas halide_scopy_impl)

halide_add_aot_library(halide_dcopy_impl
	GENERATOR_TARGET blas_l1.generator
	GENERATOR_NAME daxpy 
	GENERATED_FUNCTION halide_dcopy_impl
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host vectorize=true scale_x=false add_to_y=false
)
halide_add_aot_library_dependency(halide_blas halide_dcopy_impl)

halide_add_aot_library(halide_sscal_impl
	GENERATOR_TARGET blas_l1.generator
	GENERATOR_NAME saxpy 
	GENERATED_FUNCTION halide_sscal_impl
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host vectorize=true scale_x=true add_to_y=false
)
halide_add_aot_library_dependency(halide_blas halide_sscal_impl)

halide_add_aot_library(halide_dscal_impl
	GENERATOR_TARGET blas_l1.generator
	GENERATOR_NAME daxpy 
	GENERATED_FUNCTION halide_dscal_impl
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host vectorize=true scale_x=true add_to_y=false
)
halide_add_aot_library_dependency(halide_blas halide_dscal_impl)

halide_add_aot_library(halide_saxpy_impl
	GENERATOR_TARGET blas_l1.generator
	GENERATOR_NAME saxpy 
	GENERATED_FUNCTION halide_saxpy_impl
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host vectorize=true scale_x=true add_to_y=true
)
halide_add_aot_library_dependency(halide_blas halide_saxpy_impl)

halide_add_aot_library(halide_daxpy_impl
	GENERATOR_TARGET blas_l1.generator
	GENERATOR_NAME daxpy 
	GENERATED_FUNCTION halide_daxpy_impl
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host vectorize=true scale_x=true add_to_y=true
)
halide_add_aot_library_dependency(halide_blas halide_daxpy_impl)

halide_add_aot_library(halide_sdot
	GENERATOR_TARGET blas_l1.generator
	GENERATOR_NAME sdot 
	GENERATED_FUNCTION halide_sdot
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host vectorize=true
)
halide_add_aot_library_dependency(halide_blas halide_sdot)

halide_add_aot_library(halide_ddot
	GENERATOR_TARGET blas_l1.generator
	GENERATOR_NAME ddot 
	GENERATED_FUNCTION halide_ddot
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host vectorize=true
)
halide_add_aot_library_dependency(halide_blas halide_ddot)

halide_add_aot_library(halide_sasum
	GENERATOR_TARGET blas_l1.generator
	GENERATOR_NAME sasum 
	GENERATED_FUNCTION halide_sasum
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host vectorize=true
)
halide_add_aot_library_dependency(halide_blas halide_sasum)

halide_add_aot_library(halide_dasum
	GENERATOR_TARGET blas_l1.generator
	GENERATOR_NAME dasum 
	GENERATED_FUNCTION halide_dasum
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host vectorize=true
)
halide_add_aot_library_dependency(halide_blas halide_dasum)

halide_add_aot_library(halide_sgemv_notrans
	GENERATOR_TARGET blas_l2.generator
	GENERATOR_NAME sgemv 
	GENERATED_FUNCTION halide_sgemv_notrans
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host parallel=false vectorize=true transpose=false
)
halide_add_aot_library_dependency(halide_blas halide_sgemv_notrans)

halide_add_aot_library(halide_dgemv_notrans
	GENERATOR_TARGET blas_l2.generator
	GENERATOR_NAME dgemv 
	GENERATED_FUNCTION halide_dgemv_notrans
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host parallel=false vectorize=true transpose=false
)
halide_add_aot_library_dependency(halide_blas halide_dgemv_notrans)

halide_add_aot_library(halide_sgemv_trans
	GENERATOR_TARGET blas_l2.generator
	GENERATOR_NAME sgemv 
	GENERATED_FUNCTION halide_sgemv_trans
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host parallel=false vectorize=true transpose=true
)
halide_add_aot_library_dependency(halide_blas halide_sgemv_trans)

halide_add_aot_library(halide_dgemv_trans
	GENERATOR_TARGET blas_l2.generator
	GENERATOR_NAME dgemv 
	GENERATED_FUNCTION halide_dgemv_trans
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host parallel=false vectorize=true transpose=true
)
halide_add_aot_library_dependency(halide_blas halide_dgemv_trans)

halide_add_aot_library(halide_sger_impl
	GENERATOR_TARGET blas_l2.generator
	GENERATOR_NAME sger 
	GENERATED_FUNCTION halide_sger_impl
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host parallel=false vectorize=true
)
halide_add_aot_library_dependency(halide_blas halide_sger_impl)

halide_add_aot_library(halide_dger_impl
	GENERATOR_TARGET blas_l2.generator
	GENERATOR_NAME dger 
	GENERATED_FUNCTION halide_dger_impl
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host parallel=false vectorize=true
)
halide_add_aot_library_dependency(halide_blas halide_dger_impl)

halide_add_aot_library(halide_sgemm_notrans
	GENERATOR_TARGET blas_l3.generator
	GENERATOR_NAME sgemm 
	GENERATED_FUNCTION halide_sgemm_notrans
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host transpose_A=false transpose_B=false
)
halide_add_aot_library_dependency(halide_blas halide_sgemm_notrans)

halide_add_aot_library(halide_dgemm_notrans
	GENERATOR_TARGET blas_l3.generator
	GENERATOR_NAME dgemm 
	GENERATED_FUNCTION halide_dgemm_notrans
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host transpose_A=false transpose_B=false
)
halide_add_aot_library_dependency(halide_blas halide_dgemm_notrans)

halide_add_aot_library(halide_sgemm_transA
	GENERATOR_TARGET blas_l3.generator
	GENERATOR_NAME sgemm 
	GENERATED_FUNCTION halide_sgemm_transA
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host transpose_A=true transpose_B=false
)
halide_add_aot_library_dependency(halide_blas halide_sgemm_transA)

halide_add_aot_library(halide_dgemm_transA
	GENERATOR_TARGET blas_l3.generator
	GENERATOR_NAME dgemm 
	GENERATED_FUNCTION halide_dgemm_transA
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host transpose_A=true transpose_B=false
)
halide_add_aot_library_dependency(halide_blas halide_dgemm_transA)

halide_add_aot_library(halide_sgemm_transB
	GENERATOR_TARGET blas_l3.generator
	GENERATOR_NAME sgemm 
	GENERATED_FUNCTION halide_sgemm_transB
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host transpose_A=false transpose_B=true
)
halide_add_aot_library_dependency(halide_blas halide_sgemm_transB)

halide_add_aot_library(halide_dgemm_transB
	GENERATOR_TARGET blas_l3.generator
	GENERATOR_NAME dgemm 
	GENERATED_FUNCTION halide_dgemm_transB
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host transpose_A=false transpose_B=true
)
halide_add_aot_library_dependency(halide_blas halide_dgemm_transB)

halide_add_aot_library(halide_sgemm_transAB
	GENERATOR_TARGET blas_l3.generator
	GENERATOR_NAME sgemm 
	GENERATED_FUNCTION halide_sgemm_transAB
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host transpose_A=true transpose_B=true
)
halide_add_aot_library_dependency(halide_blas halide_sgemm_transAB)

halide_add_aot_library(halide_dgemm_transAB
	GENERATOR_TARGET blas_l3.generator
	GENERATOR_NAME dgemm 
	GENERATED_FUNCTION halide_dgemm_transAB
	GENERATOR_OUTPUTS ${GENERATOR_OUTPUTS}
	GENERATOR_ARGS target=host transpose_A=true transpose_B=true
)
halide_add_aot_library_dependency(halide_blas halide_dgemm_transAB)

get_target_property(halide_blas_INCLUDE_DIRS halide_blas INCLUDE_DIRECTORIES)

add_executable(blas_test
  tests/test_halide_blas.cpp
)
target_include_directories(blas_test
  PRIVATE
    src
    ${halide_blas_INCLUDE_DIRS}
)
target_compile_options(blas_test PRIVATE ${CXX_FLAGS})
target_link_libraries(blas_test
  PRIVATE
   cblas
   halide_blas
   Halide
)
