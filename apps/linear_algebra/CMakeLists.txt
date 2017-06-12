add_subdirectory(src)

# XXX
# seems like halide_blas needs to be changed or the AOT headers should be
# marked PUBLIC (since they are referenced by the public interface (halide_blas.h)
get_target_property(halide_blas_INCLUDE_DIRS halide_blas INCLUDE_DIRECTORIES)

add_subdirectory(tests)
add_subdirectory(benchmarks)
