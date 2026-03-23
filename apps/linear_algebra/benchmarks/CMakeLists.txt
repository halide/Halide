add_executable(halide_benchmarks halide_benchmarks.cpp)
target_compile_definitions(halide_benchmarks PRIVATE ENABLE_FTZ_DAZ)
target_link_libraries(halide_benchmarks PRIVATE halide_blas Halide::Tools)
set(benchmark_targets halide_benchmarks)

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
    list(APPEND benchmark_targets eigen_benchmarks)
    message(STATUS "Eigen3: Found")
else ()
    message(STATUS "Eigen3: Missing")
endif ()

foreach (blas IN LISTS FOUND_BLASES)
    set(blas_benchmarks "${blas}_benchmarks")
    add_executable(${blas_benchmarks} cblas_benchmarks.cpp)
    target_compile_definitions(${blas_benchmarks} PRIVATE "BLAS_NAME=\"${blas}\"")
    target_link_libraries(${blas_benchmarks} PRIVATE BLAS::${blas} Halide::Tools)
    list(APPEND benchmark_targets ${blas_benchmarks})
endforeach ()

# Large powers of two are a pathological case for the cache, so avoid
# them for the benchmarks.
set(blas_levels L1 L2 L3)
list(APPEND benchmark_sizes 64 128 256 512 1280 2560)
list(APPEND L1_functions scopy dcopy sscal dscal saxpy daxpy sdot ddot sasum dasum)
list(APPEND L2_functions sgemv_notrans dgemv_notrans sgemv_trans dgemv_trans sger dger)
list(APPEND L3_functions sgemm_notrans dgemm_notrans sgemm_transA dgemm_transA sgemm_transB dgemm_transB sgemm_transAB dgemm_transAB)

foreach (benchmark IN LISTS benchmark_targets)
    string(REPLACE "_benchmarks" "" vendor "${benchmark}")
    foreach (level IN LISTS blas_levels)
        foreach (func IN LISTS ${level}_functions)
            foreach (size IN LISTS benchmark_sizes)
                set(test_name ${vendor}_${func}_${size})

                add_test(NAME ${test_name}
                         COMMAND ${benchmark} ${func} ${size})

                set_tests_properties("${test_name}" PROPERTIES
                                     LABELS "linear_algebra;${vendor};${level};slow_tests"
                                     PASS_REGULAR_EXPRESSION "${func}[ \t]+${size}"
                                     SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
            endforeach ()
        endforeach ()
    endforeach ()
endforeach ()
