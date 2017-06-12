set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(MY_CXX_FLAGS -O3 -Wall -std=c++11 -march=native)

add_executable(halide_benchmarks
  halide_benchmarks.cpp
)
target_include_directories(halide_benchmarks
  PRIVATE
    ../src # XXX
    ${halide_blas_INCLUDE_DIRS}
)
target_compile_options(halide_benchmarks PRIVATE ${MY_CXX_FLAGS})
target_compile_definitions(halide_benchmarks PRIVATE -DUSE_HALIDE)
target_link_libraries(halide_benchmarks
  PRIVATE
    halide_blas
)
list(APPEND BLAS_NAMES halide)

find_package(Eigen3)
if (Eigen3_FOUND)
  message(STATUS "Found Eigen")
  add_executable(eigen_benchmarks
    eigen_benchmarks.cpp
  )
  target_compile_options(eigen_benchmarks PRIVATE ${MY_CXX_FLAGS} -DEIGEN_DONT_PARALLELIZE)
  target_link_libraries(eigen_benchmarks
    PRIVATE
      Eigen3::Eigen
  )
  list(APPEND BLAS_NAMES eigen)
endif()

# XXX
#  OpenBLAS seems to provide cblas symbols
#  Atlas requires also linking against its provided libcblas for cblas symbols
set(ATLAS_EXTRA_LIBS cblas)
set(OpenBLAS_EXTRA_LIBS)

# TODO there are more vendors we could add here
foreach(BLAS_VENDOR OpenBLAS ATLAS)
  set(BLA_VENDOR ${BLAS_VENDOR})
  message(STATUS "Checking BLAS ${BLAS_VENDOR}")
  find_package(BLAS)
  if (BLAS_FOUND)
    message(STATUS "Found BLAS ${BLA_VENDOR}")
    string(TOLOWER ${BLAS_VENDOR} NAME)
    set(TARGET ${NAME}_benchmarks)
    add_executable(${TARGET}
      cblas_benchmarks.cpp
    )
    target_include_directories(${TARGET}
      PRIVATE
    )
    target_compile_options(${TARGET} PRIVATE ${MY_CXX_FLAGS})
    string(TOUPPER ${BLAS_VENDOR} UNAME)
    target_compile_definitions(${TARGET} PRIVATE -DUSE_${UNAME})
    target_link_libraries(${TARGET}
      PRIVATE
       ${BLAS_LIBRARIES}
       ${${BLAS_VENDOR}_EXTRA_LIBS}
    )
    list(APPEND BLAS_NAMES ${NAME})
  endif()
endforeach()

message(STATUS "Building Benchmarks for") 
foreach(BLAS ${BLAS_NAMES})
  message(STATUS " ${BLAS}") 
endforeach()

# Large powers of two are a pathological case for the cache, so avoid
# them for the benchmarks.
list(APPEND BLAS_LEVELS l1)
list(APPEND BLAS_LEVELS l2)
list(APPEND BLAS_LEVELS l3)
list(APPEND L1_BENCHMARK_SIZES 16 64 288 1056 2080)
list(APPEND L2_BENCHMARK_SIZES 32 64 128 288 544 1056 2080)
list(APPEND L3_BENCHMARK_SIZES 32 64 128 288 544 1056 2080)
list(APPEND L1_BENCHMARKS scopy dcopy sscal dscal saxpy daxpy sdot ddot sasum dasum)
list(APPEND L2_BENCHMARKS sgemv_notrans dgemv_notrans sgemv_trans dgemv_trans sger dger)
list(APPEND L3_BENCHMARKS sgemm_notrans dgemm_notrans sgemm_transA dgemm_transA sgemm_transB dgemm_transB sgemm_transAB dgemm_transAB)

# Creates high level targets
#  ${BLAS_LEVEL}_benchmarks
#  ${BLAS_LEVEL}_benchmarks
#  ${BLAS_LEVEL}_benchmarks
# as well as
#  ${BLAS}_${BLAS_LEVEL}_benchmark
#  ${BLAS}_${BLAS_LEVEL}_benchmark_${BENCHMARK}_${BENCHMARK_SIZE}
#  ${BLAS_LEVEL}_benchmark_${BENCHMARK}
#  ${BLAS_LEVEL}_benchmark_${BENCHMARK}_${BENCHMARK_SIZE}
# e.g.
#  halide_l1_benchmark_scopy_16
# XXX unfortunately, the output is noisy. good solution?
foreach(BLAS_LEVEL ${BLAS_LEVELS})
  string(TOUPPER ${BLAS_LEVEL}_BENCHMARKS BENCHMARKS_VAR)
  string(TOUPPER ${BLAS_LEVEL}_BENCHMARK_SIZES BENCHMARK_SIZES_VAR)
  set(TARGETS)
  set(BLAS_LEVEL_TARGET ${BLAS_LEVEL}_benchmarks)
  #message(STATUS "adding target ${BLAS_LEVEL_TARGET}")
  add_custom_target(${BLAS_LEVEL_TARGET})
  foreach(BENCHMARK ${${BENCHMARKS_VAR}})
    set(BENCHMARK_TARGET ${BLAS_LEVEL}_benchmark_${BENCHMARK})
    #message(STATUS "adding target ${BENCHMARK_TARGET}")
    add_custom_target(${BENCHMARK_TARGET})
    foreach(BENCHMARK_SIZE ${${BENCHMARK_SIZES_VAR}})
      set(BENCHMARK_SIZE_TARGET ${BENCHMARK_TARGET}_${BENCHMARK_SIZE})
      add_custom_target(${BENCHMARK_SIZE_TARGET})
      #message(STATUS "adding target ${BENCHMARK_SIZE_TARGET}")

      foreach(BLAS ${BLAS_NAMES})
        set(BLAS_BENCHMARK_TARGET ${BLAS}_${BENCHMARK_TARGET})
        if (NOT TARGET ${BLAS_BENCHMARK_TARGET})
          #message(STATUS "adding target ${BLAS_BENCHMARK_TARGET}")
          add_custom_target(${BLAS_BENCHMARK_TARGET})
        endif()
        set(BLAS_LEVEL_BENCHMARK_TARGET "${BLAS}_${BLAS_LEVEL}_benchmark")
        if (NOT TARGET ${BLAS_LEVEL_BENCHMARK_TARGET})
          #message(STATUS "adding target ${BLAS_LEVEL_BENCHMARK_TARGET}")
          add_custom_target(${BLAS_LEVEL_BENCHMARK_TARGET})
        endif()
        set(BLAS_BENCHMARK_SIZE_TARGET ${BLAS}_${BENCHMARK_SIZE_TARGET})
        #message(STATUS "adding target ${BLAS_BENCHMARK_SIZE_TARGET}")
        add_custom_target(${BLAS_BENCHMARK_SIZE_TARGET}
          DEPENDS $<TARGET_FILE:${BLAS}_benchmarks>
          COMMAND $<TARGET_FILE:${BLAS}_benchmarks> ${BENCHMARK} ${BENCHMARK_SIZE}
        )
        add_dependencies(${BENCHMARK_SIZE_TARGET} ${BLAS_BENCHMARK_SIZE_TARGET})
        add_dependencies(${BLAS_BENCHMARK_TARGET} ${BLAS_BENCHMARK_SIZE_TARGET})
        add_dependencies(${BLAS_LEVEL_BENCHMARK_TARGET} ${BLAS_BENCHMARK_SIZE_TARGET})
      endforeach()
      add_dependencies(${BENCHMARK_TARGET} ${BENCHMARK_SIZE_TARGET})
    endforeach()
    add_dependencies(${BLAS_LEVEL_TARGET} ${BENCHMARK_TARGET})
  endforeach()
endforeach()
