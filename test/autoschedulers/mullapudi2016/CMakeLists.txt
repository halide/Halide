if (NOT TARGET Halide::Mullapudi2016)
    message(STATUS "Disabling mullapudi2016 tests for static Halide")
    return()
endif()

tests(GROUPS mullapudi2016 autoschedulers_cpu auto_schedule
      SOURCES
      extern.cpp
      param.cpp
      ARGS $<TARGET_FILE:Halide::Mullapudi2016>)

tests(GROUPS mullapudi2016 autoschedulers_cpu auto_schedule multithreaded
      SOURCES
      cost_function.cpp
      data_dependent.cpp
      fibonacci.cpp
      histogram.cpp
      large_window.cpp
      mat_mul.cpp
      max_filter.cpp
      multi_output.cpp
      overlap.cpp
      reorder.cpp
      small_pure_update.cpp
      tile_vs_inline.cpp
      unused_func.cpp
      vectorize_var_in_update.cpp
      ARGS $<TARGET_FILE:Halide::Mullapudi2016>)
