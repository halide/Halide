# XXX
#  if we don't set these flags, we get a major slowdown due to denormal numbers.
#  Even if the halide generators emit code that does this, we still want a fair
#   comparison against the BLAS libraries with these flags set.
#  If these flags are not set, due to randomly generated data between 0..1,
#   some benchmarks will have inconsitent timings (sscal dscal)
option(LINEAR_ALGEBRA_USE_MATH_FLAGS "Set FTZ/DAZ math flags on all linear_algebra benchmarks" ON)
if (LINEAR_ALGEBRA_USE_MATH_FLAGS)
  include (CheckCXXSourceRuns)
  CHECK_CXX_SOURCE_RUNS(
    "#include <xmmintrin.h>
    #include <pmmintrin.h>
    int main()
    {
      // Flush denormals to zero (the FTZ flag).
      _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
      // Interpret denormal inputs as zero (the DAZ flag).
      _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
      return 0;
    }" CAN_ENABLE_FTZ_DAZ)
  if (CAN_ENABLE_FTZ_DAZ)
    # explicitly set Flush-To-Zero (FTZ) and Denormals-Are-Zero (DAZ) flags
    # this definition affects macros.h
    set(MISC_DEFINITIONS -DENABLE_FTZ_DAZ)
  endif()
else()
  # Due to slowdowns, this merits a warning rather than merely a status
  message(WARNING "linear_algebra: Not setting FTZ/DAZ math flags in benchmarks")
endif()

add_executable(halide_benchmarks halide_benchmarks.cpp)
target_include_directories(halide_benchmarks PRIVATE ${halide_blas_INCLUDE_DIRS})
target_compile_definitions(halide_benchmarks PRIVATE -DUSE_HALIDE ${MISC_DEFINITIONS})
target_link_libraries(halide_benchmarks PRIVATE halide_blas)
list(APPEND BLAS_NAMES halide)

find_package(Eigen3 QUIET)
if (NOT Eigen3_FOUND)
  message(STATUS "linear_algebra: Eigen3 Missing, skipping Eigen3 benchmarks")
else()
  add_executable(eigen_benchmarks
    eigen_benchmarks.cpp
  )
  target_compile_definitions(eigen_benchmarks PRIVATE -DEIGEN_DONT_PARALLELIZE ${MISC_DEFINITIONS})
  target_compile_options(eigen_benchmarks PRIVATE -Wno-error=unused-variable)
  target_link_libraries(eigen_benchmarks
    PRIVATE
      Eigen3::Eigen
  )
  list(APPEND BLAS_NAMES eigen)
endif()

if (NOT CBLAS_FOUND)
  message(STATUS "linear_algebra: No CBLAS header, skipping BLAS benchmarks")
else()
  foreach(BLAS_VENDOR ${BLAS_VENDORS})
    set(BLA_VENDOR ${BLAS_VENDOR})
    set(BLAS_FIND_QUIETLY true)
    find_package(BLAS)
    if (NOT BLAS_FOUND)
      message(STATUS "linear_algebra: ${BLAS_VENDOR} Missing, skipping ${BLAS_VENDOR} benchmarks")
    else()
      string(TOLOWER ${BLAS_VENDOR} NAME)
      set(TARGET ${NAME}_benchmarks)
      add_executable(${TARGET}
        cblas_benchmarks.cpp
      )
      target_include_directories(${TARGET} SYSTEM
        PRIVATE
         ${CBLAS_INCLUDE_DIR}
      )
      string(TOUPPER ${BLAS_VENDOR} DEFINE_SUFFIX)
      target_compile_definitions(${TARGET} PRIVATE -DUSE_${DEFINE_SUFFIX} ${MISC_DEFINITIONS})
      target_compile_options(${TARGET} PRIVATE -Wno-error=unused-variable)
      target_link_libraries(${TARGET}
        PRIVATE
         ${BLAS_${NAME}_LIBRARY}
         ${${BLAS_VENDOR}_EXTRA_LIBS}
        )
      list(APPEND BLAS_NAMES ${NAME})
    endif()
  endforeach()
endif()

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
