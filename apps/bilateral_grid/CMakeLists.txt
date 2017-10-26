add_executable(bilateral_grid_process filter.cpp)
halide_use_image_io(bilateral_grid_process)

halide_generator(bilateral_grid.generator SRCS bilateral_grid_generator.cpp)
foreach(AUTO_SCHEDULE false true)
    if(${AUTO_SCHEDULE})
        set(LIB bilateral_grid_auto_schedule)
    else()
        set(LIB bilateral_grid)
    endif()
    halide_library_from_generator(${LIB}
                                  GENERATOR bilateral_grid.generator
                                  GENERATOR_ARGS auto_schedule=${AUTO_SCHEDULE}
                                  EXTRA_OUTPUTS stmt schedule)
    target_link_libraries(bilateral_grid_process PRIVATE ${LIB})
endforeach()
