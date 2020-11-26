add_executable(halide_benchmarks halide_benchmarks.cpp)
target_compile_definitions(halide_benchmarks PRIVATE ENABLE_FTZ_DAZ)
target_link_libraries(halide_benchmarks PRIVATE halide_blas Halide::Tools)
set(BENCHMARK_TARGETS halide_benchmarks)

find_package(Eigen3 QUIET)
set(Eigen3 Eigen3::Eigen)

if (NOT TARGET ${Eigen3})
    find_package(PkgConfig QUIET)
    if (COMMAND pkg_check_modules)
        pkg_check_modules(Eigen3 QUIET IMPORTED_TARGET eigen3)
        set(Eigen3 PkgConfig::Eigen3)
    endif ()
endif ()

if (TARGET ${Eigen3})
    add_executable(eigen_benchmarks eigen_benchmarks.cpp)
    target_compile_definitions(eigen_benchmarks PRIVATE EIGEN_DONT_PARALLELIZE ENABLE_FTZ_DAZ)
    target_link_libraries(eigen_benchmarks PRIVATE ${Eigen3} Halide::Tools)
    list(APPEND BENCHMARK_TARGETS eigen_benchmarks)
    message(STATUS "Eigen3: Found")
else ()
    message(STATUS "Eigen3: Missing")
endif ()

foreach (BLAS_TARGET IN LISTS BLAS_TARGETS)
    set(TARGET ${BLAS_TARGET}_benchmarks)
    add_executable(${TARGET} cblas_benchmarks.cpp)
    target_compile_definitions(${TARGET} PRIVATE "BLAS_NAME=\"${BLAS_TARGET}\"")
    target_link_libraries(${TARGET} PRIVATE ${BLAS_TARGET}::${BLAS_TARGET} Halide::Tools)
    list(APPEND BENCHMARK_TARGETS ${TARGET})
endforeach ()

# Large powers of two are a pathological case for the cache, so avoid
# them for the benchmarks.
set(BLAS_LEVELS L1 L2 L3)
list(APPEND BENCHMARK_SIZES 64 128 256 512 1280 2560)
list(APPEND L1_BENCHMARKS scopy dcopy sscal dscal saxpy daxpy sdot ddot sasum dasum)
list(APPEND L2_BENCHMARKS sgemv_notrans dgemv_notrans sgemv_trans dgemv_trans sger dger)
list(APPEND L3_BENCHMARKS sgemm_notrans dgemm_notrans sgemm_transA dgemm_transA sgemm_transB dgemm_transB sgemm_transAB dgemm_transAB)

foreach (TARGET IN LISTS BENCHMARK_TARGETS)
    string(REPLACE "_benchmarks" "" BLA_VENDOR "${TARGET}")
    foreach (LEVEL IN LISTS BLAS_LEVELS)
        foreach (FUNC IN LISTS ${LEVEL}_BENCHMARKS)
            foreach (SIZE IN LISTS BENCHMARK_SIZES)
                set(TEST_NAME ${BLA_VENDOR}_${FUNC}_${SIZE})
                add_test(NAME ${TEST_NAME}
                         COMMAND ${TARGET} ${FUNC} ${SIZE})
                set_tests_properties("${TEST_NAME}" PROPERTIES
                                     LABELS "${BLA_VENDOR};${LEVEL};internal_app_tests;benchmark"
                                     PASS_REGULAR_EXPRESSION "${FUNC}[ \t]+${SIZE}"
                                     SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
            endforeach ()
        endforeach ()
    endforeach ()
endforeach ()
