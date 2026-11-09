tests(
    GROUPS mullapudi2016 autoschedulers_cpu
    SOURCES extern.cpp param.cpp
    ARGS $<TARGET_FILE:Halide::Mullapudi2016>
)

tests(
    GROUPS mullapudi2016 autoschedulers_cpu multithreaded
    SOURCES
        cost_function.cpp
        data_dependent.cpp
        large_window.cpp
        multi_output.cpp
        overlap.cpp
        small_pure_update.cpp
        tile_vs_inline.cpp
        unused_func.cpp
        vectorize_var_in_update.cpp
    ARGS $<TARGET_FILE:Halide::Mullapudi2016>
)

tests(
    GROUPS mullapudi2016 autoschedulers_cpu multithreaded performance
    SOURCES
        fibonacci.cpp
        histogram.cpp
        mat_mul.cpp
        max_filter.cpp
        reorder.cpp
    ARGS $<TARGET_FILE:Halide::Mullapudi2016>
)
